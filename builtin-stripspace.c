#include "builtin.h"
#include "cache.h"
#include "parse-options.h"

/*
 * Returns the length of a line, without trailing spaces.
 *
 * If the line ends with newline, it will be removed too.
 */
static size_t cleanup(char *line, size_t len)
{
	while (len) {
		unsigned char c = line[len - 1];
		if (!isspace(c))
			break;
		len--;
	}

	return len;
}

/*
 * Remove empty lines from the beginning and end
 * and also trailing spaces from every line.
 *
 * Note that the buffer will not be NUL-terminated.
 *
 * Turn multiple consecutive empty lines between paragraphs
 * into just one empty line.
 *
 * If the input has only empty lines and spaces,
 * no output will be produced.
 *
 * If last line does not have a newline at the end, one is added.
 *
 * Enable skip_comments to skip every line starting with "#".
 */
void stripspace(struct strbuf *sb, int flag)
{
	int empties = 0;
	int skip_comments = flag & STRIP_COMMENTS;
	size_t i, j, len, newlen;
	char *eol;

	/* We may have to add a newline. */
	strbuf_grow(sb, 1);

	for (i = j = 0; i < sb->len; i += len, j += newlen) {
		eol = memchr(sb->buf + i, '\n', sb->len - i);
		len = eol ? eol - (sb->buf + i) + 1 : sb->len - i;

		if (skip_comments && len && sb->buf[i] == '#') {
			newlen = 0;
			continue;
		}
		newlen = cleanup(sb->buf + i, len);

		/* Not just an empty line? */
		if (newlen) {
			if (empties > 0 && j > 0)
				sb->buf[j++] = '\n';
			empties = 0;
			memmove(sb->buf + j, sb->buf + i, newlen);
			sb->buf[newlen + j++] = '\n';
		} else {
			empties++;
		}
	}

	strbuf_setlen(sb, j);
}

int cmd_stripspace(int argc, const char **argv, const char *prefix)
{
	struct strbuf buf;
	int flag = 0;
	struct option stripspace_options[] = {
		OPT_BIT('s', "strip-comments", &flag,
			"strip comments", STRIP_COMMENTS),
		OPT_END()
	};
	static const char * const usage[] = {
		"git-stripspace [-s] < input",
		NULL,
	};

	argc = parse_options(argc, argv, stripspace_options, usage, 0);
	strbuf_init(&buf, 0);
	if (strbuf_read(&buf, 0, 1024) < 0)
		die("could not read the input");

	stripspace(&buf, flag);

	write_or_die(1, buf.buf, buf.len);
	strbuf_release(&buf);
	return 0;
}
