#!/bin/sh

test_description='shared repository workflow, not sending too many'

. ./test-lib.sh

commit () {
	test_tick &&
	echo >file "$1" &&
	git add file &&
	git commit -m "$1"
}

test_expect_success setup '

	test_create_repo shared &&
	(
		cd shared &&
		for i in 1 2 3 4 5 6 7 8 9 10
		do
			commit $i || exit 1
		done
	) &&

	git clone shared one &&
	git clone shared two &&

	(
		cd one &&
		for i in 11 12 13 14
		do
			commit $i || exit 1
		done
		git push origin master
	) &&

	(
		cd shared &&
		git fsck-objects --full &&
		git prune
	)
'

test_expect_success 'push from side' '

	(
		cd two &&
		for i in 15 16 17
		do
			commit $i || exit 1
		done
		git push --verbose origin master:refs/heads/side 2>../log
	) &&
	tr "\015" "\012" < log >out &&
	grep "^Total 9 " out

'

test_done
