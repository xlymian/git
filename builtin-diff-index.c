#include "cache.h"
#include "diff.h"
#include "commit.h"
#include "revision.h"
#include "builtin.h"

static const char diff_cache_usage[] =
"git-diff-index [-m] [--cached] "
"[<common diff options>] <tree-ish> [<path>...]"
COMMON_DIFF_OPTIONS_HELP;

int cmd_diff_index(int argc, const char **argv, const char *prefix)
{
	struct rev_info rev;
	int cached = 0;
	int i;

	init_revisions(&rev, prefix);
	git_config(git_default_config); /* no "diff" UI options */
	rev.abbrev = 0;

	argc = setup_revisions(argc, argv, &rev, NULL);
	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
			
		if (!strcmp(arg, "--cached"))
			cached = 1;
		else
			usage(diff_cache_usage);
	}
	if (!rev.diffopt.output_format)
		rev.diffopt.output_format = DIFF_FORMAT_RAW;

	/*
	 * Make sure there is one revision (i.e. pending object),
	 * and there is no revision filtering parameters.
	 */
	if (rev.pending.nr != 1 ||
	    rev.max_count != -1 || rev.min_age != -1 || rev.max_age != -1)
		usage(diff_cache_usage);
	if (read_cache() < 0) {
		perror("read_cache");
		return -1;
	}
	return run_diff_index(&rev, cached);
}