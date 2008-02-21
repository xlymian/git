/*
 * "Nu" merge backend.
 * Copyright (c) 2007, Junio C Hamano
 */

#include "cache.h"
#include "tree.h"
#include "diff.h"
#include "diffcore.h"

static inline int same(struct diff_filespec *a, struct diff_filespec *b)
{
	return ((a->mode && b->mode) && !hashcmp(a->sha1, b->sha1));
}

static int resolve_to(struct index_state *istate,
		      struct diff_filespec *it,
		      const char *path)
{
	/*
	 * The "path" is resolved to "it".  Reflect that to the
	 * resulting index.
	 *
	 * Make sure that the index entry at path matches with
	 * HEAD, and if it is different from what we are resolving
	 * to (i.e. "it"), the work tree is also clean, before doing so.
	 */

	return -1; /* not yet */
}

static int stage_to(struct index_state *istate,
		    struct diff_filespec *stage_1,
		    struct diff_filespec *stage_2,
		    struct diff_filespec *stage_3,
		    const char *path)
{
	/*
	 * The changes to "path" are conflicting.
	 */

	return -1; /* not yet */
}

static int merge_at(struct index_state *istate,
		    struct diff_filespec *base,
		    struct diff_filespec *our,
		    struct diff_filespec *their,
		    const char *path)
{
	/*
	 * Did they delete it?
	 */
	if (!their->mode) {
		if (!our->mode) {
			/* We both deleted; keep it deleted */
			return 0;
		} else if (same(base, our)) {
			/* We did not touch; let them delete in our tree */
			return resolve_to(istate, NULL, path);
		} else {
			/* We modified while they deleted */
			return stage_to(istate, base, NULL, their, path);
		}
	}

	/*
	 * Did they create it?
	 */
	if (!base->mode) {
		if (!our->mode) {
			/* We didn't; let them create in our tree */
			return resolve_to(istate, their, path);
		} else if (same(their, our)) {
			/* Both of us created the same way; keep it */
			return 0;
		} else {
			/* Created differently; needs a two-way merge */
			return stage_to(istate, NULL, our, their, path);
		}
	}

	/*
	 * They touched an existing file.
	 */
	if (same(their, our)) {
		/* The same contents; keep it */
		return 0;
	}

	if (!our->mode) {
		/* We deleted while they modified */
		return stage_to(istate, base, NULL, their, path);
	}

	if (same(base, our)) {
		/* We did not touch; let them modify in our tree */
		return resolve_to(istate, their, path);
	}

	/* Otherwise we would need a 3-way merge */
	return stage_to(istate, base, our, their, path);
}

static int merge_single_change(struct index_state *istate,
			       struct diff_filepair *our_pair,
			       struct diff_filepair *their_pair)
{
	/*
	 * Structural 3-way merge.  Determine where in the final tree
	 * the resulting contents should go.  "our_pair" could be NULL
	 * when we do not have a matching filepair (i.e. our side did
	 * not modify since the common ancestor).
	 */
	struct diff_filespec *base = their_pair->one;
	struct diff_filespec *our = our_pair ? our_pair->two : their_pair->one;
	struct diff_filespec *their = their_pair->two;

	/*
	 * If they renamed while we didn't, our path need to be
	 * moved, but we may have local untracked but unignored
	 * files (NEEDSWORK: we may need to introduce "precious"
	 * untracked files later) or tracked files that may interfere
	 * with it.
	 */
	if (DIFF_PAIR_RENAME(their_pair)) {
		if (!our_pair || !DIFF_PAIR_RENAME(our_pair)) {
			/* We didn't -- take their rename */
			return (resolve_to(istate, NULL, our->path) |
				merge_at(istate, base, our, their,
					 their->path));
		} else if (!strcmp(our->path, their->path)) {
			/* Renamed the same way as ours */
			return merge_at(istate, base, our, their, our->path);
		} else {
			/* Renamed differently */
			return (stage_to(istate, base, our, NULL,
					 our->path) |
				stage_to(istate, base, NULL, their,
					 their->path));
		}
	}
	else {
		return merge_at(istate, base, our, their, our->path);
	}
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

static int match_changes(struct index_state *istate,
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
	int status = 0;

	for (i = 0; i < their_change->nr; i++) {
		struct diff_filepair *ours, *theirs;

		theirs = their_change->queue[i];
		ours = match_filepair(our_change, their_change->queue[i]);
		status |= merge_single_change(istate, ours, theirs);
	}

	return status;
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
	struct index_state istate;
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

	memset(&istate, 0, sizeof(istate));
	if (read_index(&istate))
		return error("cannot read the index file");

	status = match_changes(&istate, &our_changes, &their_changes);

	for (i = 0; i < our_changes.nr; i++)
		diff_free_filepair(our_changes.queue[i]);
	free(our_changes.queue);
	for (i = 0; i < their_changes.nr; i++)
		diff_free_filepair(their_changes.queue[i]);
	free(their_changes.queue);

	return status;
}
