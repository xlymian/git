/*
 * "Nu" merge backend.
 * Copyright (c) 2007, Junio C Hamano
 */

#include "cache.h"
#include "tree.h"
#include "diff.h"
#include "diffcore.h"

struct merge_nu_ce {
	const unsigned char *sha1;
	unsigned mode;
};

struct merge_nu_ent {
	const char *our_path;
	const char *result_path;
	struct merge_nu_ce base;
	struct merge_nu_ce ours;
	struct merge_nu_ce theirs;

	enum {
		NU_STRUCTURE_OURS = 0,
		NU_STRUCTURE_THEIRS,
		NU_STRUCTURE_CONFLICT_OURS,
		NU_STRUCTURE_CONFLICT_THEIRS,
	} structure_result;
	enum {
		NU_OURS = 0,
		NU_THEIRS,
		NU_ADD,
		NU_DELETE,
		NU_MERGE,
		NU_DELETE_MODIFY,
		NU_MODIFY_DELETE,
		NU_ADD_ADD,
	} content_result;

	unsigned result_conflict : 1;
	unsigned long result_size;
	char *result_data;
};

struct merge_nu {
	int nr;
	int alloc;
	struct merge_nu_ent *ent;
};

static void clear_nu(struct merge_nu *nu)
{
	nu->nr = 0;
	nu->alloc = 0;
	free(nu->ent);
}

static struct merge_nu_ent *alloc_nu_ent(struct merge_nu *nu)
{
	int pos = nu->nr++;
	struct merge_nu_ent *ent;

	ALLOC_GROW(nu->ent, nu->nr, nu->alloc);
	ent = &(nu->ent[pos]);
	memset(ent, 0, sizeof(*ent));
	return ent;
}

static inline int same(struct diff_filespec *a, struct diff_filespec *b)
{
	return ((a->mode && b->mode) && !hashcmp(a->sha1, b->sha1));
}

static void merge_entry(struct merge_nu_ent *ent,
			struct diff_filespec *base,
			struct diff_filespec *our,
			struct diff_filespec *their)
{
	/*
	 * The changes "our" and "their" correspond to what happened
	 * to the common ancestor on both sides.  We compute how our
	 * tree should be transformed to reach the merge result.
	 *
	 * Note that we will never see cases where their side did not
	 * do anything here, as the caller is walking their changes.
	 *
	 * There are three things they could have done.  Delete,
	 * modify (possibly with rename), or create.
	 */

	/* Record basic 3-way merge information */
	ent->base.mode = base->mode;
	ent->base.sha1 = base->sha1;
	ent->ours.mode = our->mode;
	ent->ours.sha1 = our->sha1;
	ent->theirs.mode = their->mode;
	ent->theirs.sha1 = their->sha1;

	/*
	 * Did they delete it?
	 */
	if (!their->mode) {
		if (!our->mode)
			/* We both deleted; keep it deleted */
			ent->content_result = NU_OURS;
		else if (same(base, our))
			/* We did not touch; let them delete in our tree */
			ent->content_result = NU_DELETE;
		else
			/* We modified while they deleted */
			ent->content_result = NU_MODIFY_DELETE;
		return;
	}

	/*
	 * Did they create it?
	 */
	if (!base->mode) {
		if (!our->mode)
			/* We didn't; let them create in our tree */
			ent->content_result = NU_ADD;
		else if (same(their, our))
			/* Both of us created the same way; keep it */
			ent->content_result = NU_OURS;
		else
			/* Created differently; needs a two-way merge */
			ent->content_result = NU_ADD_ADD;
		return;
	}

	/*
	 * They touched an existing file.
	 */
	if (same(their, our)) {
		/* The same contents; keep it */
		ent->content_result = NU_OURS;
		return;
	}

	if (!our->mode) {
		/* We deleted; a conflict */
		ent->content_result = NU_DELETE_MODIFY;
		return;
	}

	if (same(base, our)) {
		/* We did not touch; let them modify in our tree */
		ent->content_result = NU_THEIRS;
		return;
	}

	/* Otherwise we would need a 3-way merge */
	ent->content_result = NU_MERGE;
}

static void merge_single_change(struct merge_nu *nu,
				struct diff_filepair *our_pair,
				struct diff_filepair *their_pair)
{
	struct merge_nu_ent *ent;
	struct diff_filespec *base, *our, *their;

	/*
	 * Structural 3-way merge.  Determine what path the resulting
	 * contents should go.  "our_pair" could be NULL when we do
	 * not have a matching filepair (i.e. our side did not modify
	 * since the common ancestor).
	 */
	base = their_pair->one;
	our = our_pair ? our_pair->two : their_pair->one;
	their = their_pair->two;

	ent = alloc_nu_ent(nu);
	ent->our_path = our->path;

	/* Assume everything went well */
	ent->structure_result = NU_STRUCTURE_OURS;

	/*
	 * Is there a rename involved?
	 */
	if (DIFF_PAIR_RENAME(their_pair) ||
	    (our_pair && DIFF_PAIR_RENAME(our_pair))) {
		if (!our_pair || !DIFF_PAIR_RENAME(our_pair)) {
			/* We did not rename; take their rename. */
			ent->result_path = their->path;
			ent->structure_result = NU_STRUCTURE_THEIRS;
		}
		else if (!DIFF_PAIR_RENAME(their_pair)) {
			/* They did not rename; take our rename. */
			ent->result_path = our->path;
		}
		else {
			/* Renamed differently */
			ent->result_path = our->path;
			ent->structure_result = NU_STRUCTURE_CONFLICT_OURS;
		}
	} else
		ent->result_path = our->path; /* Neither side renamed */

	/*
	 * Content level merge
	 */
	merge_entry(ent, base, our, their);

	if (ent->structure_result != NU_STRUCTURE_CONFLICT_OURS)
		return;

	/*
	 * They wanted to rename base->path to their->path while we
	 * moved it to our->path.  "ent" is our half, and we create
	 * their half now.
	 */
	ent = alloc_nu_ent(nu);
	ent->our_path = their->path;
	ent->result_path = their->path;
	merge_entry(ent, base, our, their);
	ent->structure_result = NU_STRUCTURE_CONFLICT_THEIRS;
}

static int path_compare(const void *path_, const void *pair_)
{
	const char *path = path_;
	const struct diff_filepair *pair;
	pair = *((const struct diff_filepair **)pair_);
	return strcmp(path, pair->one->path);
}

static struct diff_filepair *match_filepair(struct diff_queue_struct *q,
					    struct diff_filepair *other)
{
	/*
	 * Locate the filepair in "q" that talks about what happened to
	 * the path "other" changed.  If there is none, that means "q"
	 * left it intact, so NULL return will be treated as if it is
	 * a no-op filepair.
	 *
	 * We have sorted "q" by preimage pathname already, so we can
	 * binary search.
	 */
	void *found;

	found = bsearch(other->one->path, q->queue, q->nr,
			sizeof(q->queue[0]), path_compare);
	if (!found)
		return NULL;
	return *((struct diff_filepair **)found);
}

static void match_changes(struct merge_nu *nu,
			  struct diff_queue_struct *our_change,
			  struct diff_queue_struct *their_change)
{
	/*
	 * We have taken two diffs (common ancestor vs ours, and
	 * common ancestor vs theirs).  Go through their change, find
	 * matching change from ours, and consolidate the result into
	 * insn to transform our tree to the resulting tree.
	 */
	int i;

	for (i = 0; i < their_change->nr; i++) {
		struct diff_filepair *ours, *theirs;

		theirs = their_change->queue[i];
		ours = match_filepair(our_change, their_change->queue[i]);
		merge_single_change(nu, ours, theirs);
	}
}

static int preimage_path_cmp(const void *a_, const void *b_)
{
	const struct diff_filepair *a, *b;

	a = *(const struct diff_filepair **)a_;
	b = *(const struct diff_filepair **)b_;
	return strcmp(a->one->path, b->one->path);
}

static void add_tree_changes(struct diff_queue_struct *q,
			     struct diff_options *opts,
			     void *cb_data)
{
	/*
	 * This code happens to know that the format callback is the
	 * last thing diff_flush() does, and the only thing caller
	 * does after we return is to discard the queued diff.  We
	 * just steal the filepairs and keep to ourselves.  To be
	 * politically correct, we should be making a deep copy
	 * instead, but this is efficient and is sufficient for now...
	 */
	*((struct diff_queue_struct *)cb_data) = *q;
	memset(q, 0, sizeof(*q));

	/*
	 * Sort the resulting queue by pathname of the preimage (i.e.
	 * common ancestor).
	 */
	q = cb_data;
	qsort(q->queue, q->nr, sizeof(q->queue[0]), preimage_path_cmp);
}

static void prepare_tree_desc(struct tree_desc *desc,
			      const unsigned char *sha1,
			      void **tofree)
{
	void *tree;
	unsigned long size;

	if (!sha1) {
		/* Empty tree */
		init_tree_desc(desc, "", 0);
		*tofree = NULL;
		return;
	}

	tree = read_object_with_reference(sha1, tree_type, &size, NULL);
	if (!tree)
		die("unable to read tree: %s", sha1_to_hex(sha1));
	*tofree = tree;
	init_tree_desc(desc, tree, size);
}

static void get_tree_diff(struct diff_queue_struct *changes,
			  const struct tree *old_tree,
			  const struct tree *new_tree)
{
	struct tree_desc old, new;
	void *old_data, *new_data;
	struct diff_options opts;

	prepare_tree_desc(&old, old_tree->object.sha1, &old_data);
	prepare_tree_desc(&new, new_tree->object.sha1, &new_data);

	memset(changes, 0, sizeof(*changes));

	diff_setup(&opts);
	opts.output_format = DIFF_FORMAT_CALLBACK;
	opts.format_callback = add_tree_changes;
	opts.format_callback_data = changes;
	opts.detect_rename = DIFF_DETECT_RENAME;
	DIFF_OPT_SET(&opts, RECURSIVE);
	diff_setup_done(&opts);

	diff_tree(&old, &new, "", &opts);
	diffcore_std(&opts);
	diff_flush(&opts);
	free(old_data);
	free(new_data);
}

static int show_merge_nu(struct merge_nu *nu)
{
	int i, status;
	struct index_state nu_index;

	/*
	 * Reflect the change they made since the common ancestor
	 * to the current tree, but make sure we detect conflicting
	 * changes.
	 */
	status = -1;
	memset(&nu_index, 0, sizeof(nu_index));
	read_index(&nu_index);

	/*
	 * Their otherwise clean deletion must be prevented if we have
	 * modifications at the path.
	 *
	 * Their otherwise clean addition must be prevented if we have
	 * modifications or untracked but unignored files at places
	 * that will be nuked due to D/F conflicts.
	 */
	for (i = 0; i < nu->nr; i++) {
		const char *our_their = NULL;
		struct merge_nu_ent *ent = &(nu->ent[i]);

		fprintf(stderr, "%s: ", ent->our_path);
		if (strcmp(ent->our_path, ent->result_path))
			fprintf(stderr, "rename to %s ", ent->result_path);
		switch (ent->structure_result) {
		default:
			break;
		case NU_STRUCTURE_CONFLICT_OURS:
			our_their = "ours";
			break;
		case NU_STRUCTURE_CONFLICT_THEIRS:
			our_their = "theirs";
			break;
		}
		if (our_their)
			fprintf(stderr, "rename conflict: %s half: %s: ",
				our_their, ent->result_path);

		switch (ent->content_result) {
		case NU_DELETE_MODIFY:
			fprintf(stderr, "delete/modify conflict");
			break;
		case NU_MODIFY_DELETE:
			fprintf(stderr, "modify/delete conflict");
			break;
		case NU_ADD_ADD:
			fprintf(stderr, "add/add conflict");
			break;
		case NU_OURS:
			fprintf(stderr, "take our version");
			break;
		case NU_THEIRS:
			fprintf(stderr, "take their version");
			break;
		case NU_ADD:
			fprintf(stderr, "take their addition");
			break;
		case NU_DELETE:
			fprintf(stderr, "take their deletion");
			break;
		case NU_MERGE:
			fprintf(stderr, "merge with theirs");
			break;
		default:
			fprintf(stderr, "Huh?");
			break;
		}
		fprintf(stderr, "\n");
	}
	discard_index(&nu_index);
	return status;
}

/*
 * Take "diff -M" for common ancestor vs ours and common ancestor vs theirs
 * to figure out what happened to each of the paths in the ancestor.
 */
int replay_trees(const char *base_sha1,
		 const char *our_sha1,
		 const char *our_label,
		 const char *their_sha1,
		 const char *their_label)
{
	/*
	 * Merge changes made to their_tree since they forked at
	 * base_tree back to our_tree.  Return negative if it failed
	 * to do anything, 0 if merged cleanly, or 1 if there were
	 * conflicts.  Non negative return cases should write the
	 * results out to the index and the work tree files.
	 */
	struct diff_queue_struct our_changes;
	struct diff_queue_struct their_changes;
	struct merge_nu nu;
	struct tree *base_tree, *our_tree, *their_tree;
	int i, status;

	if (!(base_tree = (struct tree *)peel_to_type(base_sha1, 0, NULL, OBJ_TREE)))
		return error("base tree '%s' not found", base_sha1);
	if (!(our_tree = (struct tree *)peel_to_type(our_sha1, 0, NULL, OBJ_TREE)))
		return error("our tree '%s' not found", our_sha1);
	if (!(their_tree = (struct tree *)peel_to_type(their_sha1, 0, NULL, OBJ_TREE)))
		return error("their tree '%s' not found", their_sha1);
	get_tree_diff(&our_changes, base_tree, our_tree);
	get_tree_diff(&their_changes, base_tree, their_tree);

	memset(&nu, 0, sizeof(nu));
	clear_nu(&nu);
	match_changes(&nu, &our_changes, &their_changes);

	/*
	 * NEEDSWORK: merge_nu[] contains list of insns to transform
	 * our tree into merge result.  There may however be insns to
	 * cause D/F conflicts, in which case they need to be fuzzed
	 * when we are building a virtual common ancestor tree.
	 */
	status = show_merge_nu(&nu);

	for (i = 0; i < our_changes.nr; i++)
		diff_free_filepair(our_changes.queue[i]);
	free(our_changes.queue);
	for (i = 0; i < their_changes.nr; i++)
		diff_free_filepair(their_changes.queue[i]);
	free(their_changes.queue);

	return status;
}
