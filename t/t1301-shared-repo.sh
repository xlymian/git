#!/bin/sh
#
# Copyright (c) 2007 Johannes Schindelin
#

test_description='Test shared repository initialization'

. ./test-lib.sh

# User must have read permissions to the repo -> failure on --shared=0400
test_expect_success 'shared = 0400 (faulty permission u-w)' '
	mkdir sub && (
		cd sub && git init --shared=0400
	)
	ret="$?"
	rm -rf sub
	test $ret != "0"
'

test_expect_success 'shared=all' '
	mkdir sub &&
	cd sub &&
	git init --shared=all &&
	test 2 = $(git config core.sharedrepository)
'

test_expect_success 'update-server-info honors core.sharedRepository' '
	: > a1 &&
	git add a1 &&
	test_tick &&
	git commit -m a1 &&
	umask 0277 &&
	git update-server-info &&
	actual="$(ls -l .git/info/refs)" &&
	case "$actual" in
	-r--r--r--*)
		: happy
		;;
	*)
		echo Oops, .git/info/refs is not 0444
		false
		;;
	esac
'

for u in	0660:rw-rw---- \
		0640:rw-r----- \
		0600:rw------- \
		0666:rw-rw-rw- \
		0664:rw-rw-r--
do
	x=$(expr "$u" : ".*:\([rw-]*\)") &&
	y=$(echo "$x" | sed -e "s/w/-/g") &&
	u=$(expr "$u" : "\([0-7]*\)") &&
	git config core.sharedrepository "$u" &&
	umask 0277 &&

	test_expect_success "shared = $u ($y) ro" '

		rm -f .git/info/refs &&
		git update-server-info &&
		actual="$(ls -l .git/info/refs)" &&
		actual=${actual%% *} &&
		test "x$actual" = "x-$y" || {
			ls -lt .git/info
			false
		}
	'

	umask 077 &&
	test_expect_success "shared = $u ($x) rw" '

		rm -f .git/info/refs &&
		git update-server-info &&
		actual="$(ls -l .git/info/refs)" &&
		actual=${actual%% *} &&
		test "x$actual" = "x-$x" || {
			ls -lt .git/info
			false
		}

	'

done

test_done
