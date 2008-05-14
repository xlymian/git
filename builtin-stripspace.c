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
 * Is this a "XXX-by: name <e-mail@do.main>" line?
 */
static int is_signoff(char *line, size_t len)
{
	size_t i;
	int seen_at;

	/* Does it end with <e-mail@do.main>? */
	if (!len || line[len - 1] != '>')
		return 0;
	seen_at = 0;
	for (i = len - 2; 0 <= i; i--) {
		char ch = line[i];
		if (ch == '@')
			seen_at++;
		else if (ch == '<')
			break;
	}
	if (!i || seen_at != 1)
		return 0;

	/* Is it "frotz-by:" ? */
	for (i = 0; i < len; i++) {
		char ch = line[i];
		if (isalnum(ch) || ch == '-')
			continue;
		if (ch == ':')
			break;
		return 0;
	}
	if ((len <= i) || (i < 3) || memcmp(line + i - 3, "-by:", 4))
		return 0;
	return 1;
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
	int empties = 0, was_signoff = 0;
	int skip_comments = flag & STRIP_COMMENTS;
	int clean_log = flag & STRIP_CLEAN_LOG;
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
			if (clean_log && is_signoff(sb->buf + i, newlen)) {
				if (!was_signoff) {
					/*
					 * Make sure we insert a LF if the
					 * previous was not a sign-off.
					 */
					if (!empties && (i == j)) {
						/* yuck, we need to grow */
						strbuf_splice(sb, i, 0, "Q", 1);
						i++;
					}
					empties++;
				}
				was_signoff = 1;
			} else {
				was_signoff = 0;
			}
			if (empties > 0 && j > 0)
				sb->buf[j++] = '\n';
			empties = 0;
			memmove(sb->buf + j, sb->buf + i, newlen);
			sb->buf[newlen + j++] = '\n';
		} else if (was_signoff) {
			; /* strip empty after signed-off-by */
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
		OPT_BIT('l', "log-clean", &flag,
			"clean log message", STRIP_CLEAN_LOG),
		OPT_END()
	};
	static const char * const usage[] = {
		"git-stripspace [-s] [-l] < input",
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
