#!/bin/sh

test_description='git am sign-offs'

. ./test-lib.sh

test_expect_success setup '

	echo hello > world &&
	git add world &&
	test_tick &&
	git commit -m initial &&
	git tag initial

'


test_expect_success 'applying straight' '

	git reset --hard initial &&
	git am ../t4300/01-patch.txt &&
	git cat-file commit HEAD | sed -e "1,/^$/d" >actual &&
	diff -u ../t4300/01-expect actual

'

test_expect_success 'applying with -s' '

	git reset --hard initial &&
	git am -s ../t4300/02-patch.txt &&
	git cat-file commit HEAD | sed -e "1,/^$/d" >actual &&
	diff -u ../t4300/02-expect actual

'

test_expect_success 'applying with -s omits duplicates' '

	git reset --hard initial &&
	git am -s ../t4300/03-patch.txt &&
	git cat-file commit HEAD | sed -e "1,/^$/d" >actual &&
	diff -u ../t4300/03-expect actual

'

test_expect_success 'applying with -s adds sign-off' '

	git reset --hard initial &&
	git am -s ../t4300/04-patch.txt &&
	git cat-file commit HEAD | sed -e "1,/^$/d" >actual &&
	diff -u ../t4300/04-expect actual

'

test_expect_failure 'apply fixes up s-o-b line (1)' '

	git reset --hard initial &&
	git am -s ../t4300/05-patch.txt &&
	git cat-file commit HEAD | sed -e "1,/^$/d" >actual &&
	diff -u ../t4300/05-expect actual

'

test_expect_failure 'apply fixes up s-o-b line (2)' '

	git reset --hard initial &&
	git am ../t4300/06-patch.txt &&
	git cat-file commit HEAD | sed -e "1,/^$/d" >actual &&
	diff -u ../t4300/06-expect actual

'

test_expect_success 'apply with author'\''s s-o-b' '

	git reset --hard initial &&
	git am -s --forge ../t4300/07-patch.txt &&
	git cat-file commit HEAD | sed -e "1,/^$/d" >actual &&
	diff -u ../t4300/07-expect actual

'

test_done
