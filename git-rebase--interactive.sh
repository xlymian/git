#!/bin/sh
#
# Copyright (c) 2006 Johannes E. Schindelin

# SHORT DESCRIPTION
#
# This script makes it easy to fix up commits in the middle of a series,
# and rearrange commits.
#
# The original idea comes from Eric W. Biederman, in
# http://article.gmane.org/gmane.comp.version-control.git/22407

USAGE='(--continue | --abort | --skip | [--preserve-merges] [--first-parent]
	[--preserve-tags] [--verbose] [--onto <branch>] <upstream> [<branch>])'

OPTIONS_SPEC=
. git-sh-setup
require_work_tree

DOTEST="$GIT_DIR/.dotest-merge"
TODO="$DOTEST"/git-rebase-todo
DONE="$DOTEST"/done
MSG="$DOTEST"/message
SQUASH_MSG="$DOTEST"/message-squash
PRESERVE_MERGES=
STRATEGY=
VERBOSE=
test -f "$DOTEST"/strategy && STRATEGY="$(cat "$DOTEST"/strategy)"
test -f "$DOTEST"/verbose && VERBOSE=t

GIT_CHERRY_PICK_HELP="  After resolving the conflicts,
mark the corrected paths with 'git add <paths>', and
run 'git rebase --continue'"
export GIT_CHERRY_PICK_HELP

mark_prefix=refs/rebase-marks/

warn () {
	echo "$*" >&2
}

output () {
	case "$VERBOSE" in
	'')
		output=$("$@" 2>&1 )
		status=$?
		test $status != 0 && printf "%s\n" "$output"
		return $status
		;;
	*)
		"$@"
		;;
	esac
}

require_clean_work_tree () {
	# test if working tree is dirty
	git rev-parse --verify HEAD > /dev/null &&
	git update-index --refresh &&
	git diff-files --quiet &&
	git diff-index --cached --quiet HEAD -- ||
	die "Working tree is dirty"
}

ORIG_REFLOG_ACTION="$GIT_REFLOG_ACTION"

comment_for_reflog () {
	case "$ORIG_REFLOG_ACTION" in
	''|rebase*)
		GIT_REFLOG_ACTION="rebase -i ($1)"
		export GIT_REFLOG_ACTION
		;;
	esac
}

last_count=
mark_action_done () {
	sed -e 1q < "$TODO" >> "$DONE"
	sed -e 1d < "$TODO" >> "$TODO".new
	mv -f "$TODO".new "$TODO"
	count=$(grep -c '^[^#]' < "$DONE")
	total=$(($count+$(grep -c '^[^#]' < "$TODO")))
	if test "$last_count" != "$count"
	then
		last_count=$count
		printf "Rebasing (%d/%d)\r" $count $total
		test -z "$VERBOSE" || echo
	fi
}

make_patch () {
	parent_sha1=$(git rev-parse --verify "$1"^) ||
		die "Cannot get patch for $1^"
	git diff-tree -p "$parent_sha1".."$1" > "$DOTEST"/patch
	test -f "$DOTEST"/message ||
		git cat-file commit "$1" | sed "1,/^$/d" > "$DOTEST"/message
	test -f "$DOTEST"/author-script ||
		get_author_ident_from_commit "$1" > "$DOTEST"/author-script
}

die_with_patch () {
	make_patch "$1"
	git rerere
	die "$2"
}

cleanup_before_quit () {
	for ref in $(git for-each-ref --format='%(refname)' "${mark_prefix%/}")
	do
		git update-ref -d "$ref" "$ref" || return 1
	done
	rm -rf "$DOTEST"
}

die_abort () {
	cleanup_before_quit
	die "$1"
}

has_action () {
	grep '^[^#]' "$1" >/dev/null
}

redo_merge () {
	rm_sha1=$1
	shift

	eval "$(get_author_ident_from_commit $rm_sha1)"
	msg="$(git cat-file commit $rm_sha1 | sed -e '1,/^$/d')"

	if ! GIT_AUTHOR_NAME="$GIT_AUTHOR_NAME" \
		GIT_AUTHOR_EMAIL="$GIT_AUTHOR_EMAIL" \
		GIT_AUTHOR_DATE="$GIT_AUTHOR_DATE" \
		output git merge $STRATEGY -m "$msg" "$@"
	then
		git rerere
		printf "%s\n" "$msg" > "$GIT_DIR"/MERGE_MSG
		die Error redoing merge $rm_sha1
	fi
	unset rm_sha1
}

pick_one () {
	no_ff=
	case "$1" in -n) sha1=$2; no_ff=t ;; *) sha1=$1 ;; esac
	output git rev-parse --verify $sha1 || die "Invalid commit name: $sha1"
	parent_sha1=$(git rev-parse --verify $sha1^) ||
		die "Could not get the parent of $sha1"
	current_sha1=$(git rev-parse --verify HEAD)
	if test "$no_ff$current_sha1" = "$parent_sha1"; then
		output git reset --hard $sha1
		test "a$1" = a-n && output git reset --soft $current_sha1
		sha1=$(git rev-parse --short $sha1)
		output warn Fast forward to $sha1
	else
		output git cherry-pick "$@"
	fi
}

nth_string () {
	case "$1" in
	*1[0-9]|*[04-9]) echo "$1"th;;
	*1) echo "$1"st;;
	*2) echo "$1"nd;;
	*3) echo "$1"rd;;
	esac
}

make_squash_message () {
	if test -f "$SQUASH_MSG"; then
		COUNT=$(($(sed -n "s/^# This is [^0-9]*\([1-9][0-9]*\).*/\1/p" \
			< "$SQUASH_MSG" | sed -ne '$p')+1))
		echo "# This is a combination of $COUNT commits."
		sed -e 1d -e '2,/^./{
			/^$/d
		}' <"$SQUASH_MSG"
	else
		COUNT=2
		echo "# This is a combination of two commits."
		echo "# The first commit's message is:"
		echo
		git cat-file commit HEAD | sed -e '1,/^$/d'
	fi
	echo
	echo "# This is the $(nth_string $COUNT) commit message:"
	echo
	git cat-file commit $1 | sed -e '1,/^$/d'
}

peek_next_command () {
	sed -n "1s/ .*$//p" < "$TODO"
}

mark_to_ref () {
	if expr "$1" : "^:[0-9][0-9]*$" >/dev/null
	then
		echo "$mark_prefix$(printf %d ${1#:})"
	else
		echo "$1"
	fi
}

do_next () {
	rm -f "$DOTEST"/message "$DOTEST"/author-script \
		"$DOTEST"/amend || exit
	read command sha1 rest < "$TODO"
	case "$command" in
	'#'*|'')
		mark_action_done
		;;
	pick|p)
		comment_for_reflog pick

		mark_action_done
		pick_one $sha1 ||
			die_with_patch $sha1 "Could not apply $sha1... $rest"
		;;
	edit|e)
		comment_for_reflog edit

		mark_action_done
		pick_one $sha1 ||
			die_with_patch $sha1 "Could not apply $sha1... $rest"
		make_patch $sha1
		: > "$DOTEST"/amend
		warn
		warn "You can amend the commit now, with"
		warn
		warn "	git commit --amend"
		warn
		warn "Once you are satisfied with your changes, run"
		warn
		warn "	git rebase --continue"
		warn
		exit 0
		;;
	squash|s)
		comment_for_reflog squash

		has_action "$DONE" ||
			die "Cannot 'squash' without a previous commit"

		mark_action_done
		make_squash_message $sha1 > "$MSG"
		case "$(peek_next_command)" in
		squash|s)
			EDIT_COMMIT=
			USE_OUTPUT=output
			cp "$MSG" "$SQUASH_MSG"
			;;
		*)
			EDIT_COMMIT=-e
			USE_OUTPUT=
			rm -f "$SQUASH_MSG" || exit
			;;
		esac

		failed=f
		author_script=$(get_author_ident_from_commit HEAD)
		output git reset --soft HEAD^
		pick_one -n $sha1 || failed=t
		echo "$author_script" > "$DOTEST"/author-script
		if test $failed = f
		then
			# This is like --amend, but with a different message
			eval "$author_script"
			GIT_AUTHOR_NAME="$GIT_AUTHOR_NAME" \
			GIT_AUTHOR_EMAIL="$GIT_AUTHOR_EMAIL" \
			GIT_AUTHOR_DATE="$GIT_AUTHOR_DATE" \
			$USE_OUTPUT git commit --no-verify -F "$MSG" $EDIT_COMMIT || failed=t
		fi
		if test $failed = t
		then
			cp "$MSG" "$GIT_DIR"/MERGE_MSG
			warn
			warn "Could not apply $sha1... $rest"
			die_with_patch $sha1 ""
		fi
		;;
	mark)
		mark_action_done

		mark=$(mark_to_ref :${sha1#:})
		test :${sha1#:} = "$mark" && die "Invalid mark '$sha1'"

		git rev-parse --verify "$mark" > /dev/null 2>&1 && \
			warn "mark $sha1 already exist; overwriting it"

		git update-ref "$mark" HEAD || die "update-ref failed"
		;;
	merge|m)
		comment_for_reflog merge

		if ! git rev-parse --verify $sha1 > /dev/null
		then
			die "Invalid reference merge '$sha1' in"
					"$command $sha1 $rest"
		fi

		new_parents=
		for p in $rest
		do
			new_parents="$new_parents $(mark_to_ref $p)"
		done
		new_parents="${new_parents# }"
		test -n "$new_parents" || \
			die "You forgot to give the parents for the" \
				"merge $sha1. Please fix it in $TODO"

		mark_action_done
		redo_merge $sha1 $new_parents
		;;
	reset|r)
		comment_for_reflog reset

		tmp=$(git rev-parse --verify "$(mark_to_ref $sha1)") ||
			die "Invalid parent '$sha1' in $command $sha1 $rest"

		mark_action_done
		output git reset --hard $tmp
		;;
	tag|t)
		comment_for_reflog tag

		mark_action_done
		output git tag -f "$sha1"
		;;
	*)
		warn "Unknown command: $command $sha1 $rest"
		die_with_patch $sha1 "Please fix this in the file $TODO."
		;;
	esac
	test -s "$TODO" && return

	comment_for_reflog finish &&
	HEADNAME=$(cat "$DOTEST"/head-name) &&
	OLDHEAD=$(cat "$DOTEST"/head) &&
	SHORTONTO=$(git rev-parse --short $(cat "$DOTEST"/onto)) &&
	NEWHEAD=$(git rev-parse HEAD) &&
	case $HEADNAME in
	refs/*)
		message="$GIT_REFLOG_ACTION: $HEADNAME onto $SHORTONTO)" &&
		git update-ref -m "$message" $HEADNAME $NEWHEAD $OLDHEAD &&
		git symbolic-ref HEAD $HEADNAME
		;;
	esac && {
		test ! -f "$DOTEST"/verbose ||
			git diff-tree --stat $(cat "$DOTEST"/head)..HEAD
	} &&
	cleanup_before_quit &&
	git gc --auto &&
	warn "Successfully rebased and updated $HEADNAME."

	exit
}

do_rest () {
	while :
	do
		do_next
	done
}

get_value_from_list () {
	# args: "key" " key1#value1 key2#value2"
	case "$2" in
	*" $1#"*)
		stm_tmp="${2#* $1#}"
		echo "${stm_tmp%% *}"
		unset stm_tmp
		;;
	*)
		return 1
		;;
	esac
}

insert_value_at_key_into_list () {
	# args: "value" "key" " key1#value1 key2#value2"
	case "$3 " in
	*" $2#$1 "*)
		echo "$3"
		;;
	*" $2#"*)
		echo "$3"
		return 1
		;;
	*)
		echo "$3 $2#$1"
		;;
	esac
}

create_extended_todo_list () {
	(
	if test t = "${PRESERVE_TAGS:-}"
	then
		tag_list=$(git show-ref --abbrev=7 --tags | \
			(
			while read sha1 tag
			do
				tag=${tag#refs/tags/}
				if test ${last_sha1:-0000} = $sha1
				then
					saved_tags="$saved_tags:$tag"
				else
					printf "%s" "${last_sha1:+ $last_sha1#$saved_tags}"
					last_sha1=$sha1
					saved_tags=$tag
				fi
			done
			echo "${last_sha1:+ $last_sha1:$saved_tags}"
			) )
	else
		tag_list=
	fi
	while IFS=_ read commit parents subject
	do
		if test t = "$PRESERVE_MERGES" -a \
			"${last_parent:-$commit}" != "$commit"
		then
			if test t = "${delayed_mark:-f}"
			then
				marked_commits=$(insert_value_at_key_into_list \
					dummy $last_parent "${marked_commits:-}")
				delayed_mark=f
			fi
			test "$last_parent" = $SHORTUPSTREAM && \
				last_parent=$SHORTONTO
			echo "reset $last_parent"
		fi
		last_parent="${parents%% *}"

		get_value_from_list $commit "${marked_commits:-}" \
			>/dev/null && echo mark

		if tmp=$(get_value_from_list $commit "$tag_list")
		then
			for t in $(echo $tmp | tr : ' ')
			do
				echo tag $t
			done
		fi

		case "$parents" in
		*' '*)
			delayed_mark=t
			new_parents=
			for p in ${parents#* }
			do
				marked_commits=$(insert_value_at_key_into_list \
					dummy "$p" "${marked_commits:-}")
				if test "$p" = $SHORTUPSTREAM
				then
					new_parents="$new_parents $SHORTONTO"
				else
					new_parents="$new_parents $p"
				fi
			done
			unset p
			echo merge $commit $new_parents
			unset new_parents
			;;
		*)
			echo "pick $commit $subject"
			;;
		esac
	done
	test -n "${last_parent:-}" -a "${last_parent:-}" != $SHORTUPSTREAM && \
		echo reset $last_parent
	) | \
	perl -e 'print reverse <>' | \
	while read cmd args
	do
		: ${commit_mark_list:=} ${last_commit:=000}
		case "$cmd" in
		pick)
			last_commit="${args%% *}"
			;;
		mark)
			: ${next_mark:=0}
			if commit_mark_list=$(insert_value_at_key_into_list \
				$next_mark $last_commit "$commit_mark_list")
			then
				args=":$next_mark"
				next_mark=$(($next_mark + 1))
			else
				die "Internal error: two marks for" \
					"the same commit"
			fi
			;;
		reset)
			if tmp=$(get_value_from_list $args "$commit_mark_list")
			then
				args=":$tmp"
			fi
			;;
		merge)
			new_args=
			for i in ${args#* }
			do
				if tmp=$(get_value_from_list $i \
					"$commit_mark_list")
				then
					new_args="$new_args :$tmp"
				else
					new_args="$new_args $i"
				fi
			done
			last_commit="${args%% *}"
			args="$last_commit ${new_args# }"
			;;
		esac
		echo "$cmd $args"
	done
}

while test $# != 0
do
	case "$1" in
	--continue)
		comment_for_reflog continue

		test -d "$DOTEST" || die "No interactive rebase running"

		# Sanity check
		git rev-parse --verify HEAD >/dev/null ||
			die "Cannot read HEAD"
		git update-index --refresh && git diff-files --quiet ||
			die "Working tree is dirty"

		# do we have anything to commit?
		if git diff-index --cached --quiet HEAD --
		then
			: Nothing to commit -- skip this
		else
			. "$DOTEST"/author-script ||
				die "Cannot find the author identity"
			if test -f "$DOTEST"/amend
			then
				git reset --soft HEAD^ ||
				die "Cannot rewind the HEAD"
			fi
			export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_AUTHOR_DATE &&
			git commit --no-verify -F "$DOTEST"/message -e ||
			die "Could not commit staged changes."
		fi

		require_clean_work_tree
		do_rest
		;;
	--abort)
		comment_for_reflog abort

		git rerere clear
		test -d "$DOTEST" || die "No interactive rebase running"

		HEADNAME=$(cat "$DOTEST"/head-name)
		HEAD=$(cat "$DOTEST"/head)
		case $HEADNAME in
		refs/*)
			git symbolic-ref HEAD $HEADNAME
			;;
		esac &&
		output git reset --hard $HEAD &&
		cleanup_before_quit
		exit
		;;
	--skip)
		comment_for_reflog skip

		git rerere clear
		test -d "$DOTEST" || die "No interactive rebase running"

		output git reset --hard && do_rest
		;;
	-s|--strategy)
		case "$#,$1" in
		*,*=*)
			STRATEGY="-s "$(expr "z$1" : 'z-[^=]*=\(.*\)') ;;
		1,*)
			usage ;;
		*)
			STRATEGY="-s $2"
			shift ;;
		esac
		;;
	-m|--merge)
		# we use merge anyway
		;;
	-C*)
		die "Interactive rebase uses merge, so $1 does not make sense"
		;;
	-v|--verbose)
		VERBOSE=t
		;;
	-p|--preserve-merges)
		PRESERVE_MERGES=t
		;;
	-f|--first-parent)
		FIRST_PARENT=t
		PRESERVE_MERGES=t
		;;
	-t|--preserve-tags)
		PRESERVE_TAGS=t
		;;
	-i|--interactive)
		# yeah, we know
		;;
	''|-h)
		usage
		;;
	*)
		test -d "$DOTEST" &&
			die "Interactive rebase already started"

		git var GIT_COMMITTER_IDENT >/dev/null ||
			die "You need to set your committer info first"

		comment_for_reflog start

		ONTO=
		case "$1" in
		--onto)
			ONTO=$(git rev-parse --verify "$2") ||
				die "Does not point to a valid commit: $2"
			shift; shift
			;;
		esac

		require_clean_work_tree

		if test ! -z "$2"
		then
			output git show-ref --verify --quiet "refs/heads/$2" ||
				die "Invalid branchname: $2"
			output git checkout "$2" ||
				die "Could not checkout $2"
		fi

		HEAD=$(git rev-parse --verify HEAD) || die "No HEAD?"
		UPSTREAM=$(git rev-parse --verify "$1") || die "Invalid base"

		mkdir "$DOTEST" || die "Could not create temporary $DOTEST"

		test -z "$ONTO" && ONTO=$UPSTREAM

		: > "$DOTEST"/interactive || die "Could not mark as interactive"
		git symbolic-ref HEAD > "$DOTEST"/head-name 2> /dev/null ||
			echo "detached HEAD" > "$DOTEST"/head-name

		echo $HEAD > "$DOTEST"/head
		echo $UPSTREAM > "$DOTEST"/upstream
		echo $ONTO > "$DOTEST"/onto
		test -z "$STRATEGY" || echo "$STRATEGY" > "$DOTEST"/strategy
		test t = "$VERBOSE" && : > "$DOTEST"/verbose

		SHORTUPSTREAM=$(git rev-parse --short=7 $UPSTREAM)
		SHORTHEAD=$(git rev-parse --short=7 $HEAD)
		SHORTONTO=$(git rev-parse --short=7 $ONTO)
		common_rev_list_opts="--abbrev-commit --abbrev=7
			--left-right --cherry-pick $UPSTREAM...$HEAD"
		if test t = "$PRESERVE_MERGES" -o t = "${FIRST_PARENT:-f}" \
			-o t = "${PRESERVE_TAGS:-}"
		then
			opts=
			test t = "${FIRST_PARENT:-f}" && \
				opts="$opts --first-parent"
			test t != "$PRESERVE_MERGES" && \
				opts="$opts --no-merges"
			git rev-list --pretty='format:%h_%p_%s' --topo-order \
				$opts $common_rev_list_opts | \
				grep -v ^commit | \
				create_extended_todo_list
		else
			git rev-list --no-merges --reverse --pretty=oneline \
				 $common_rev_list_opts | sed -n "s/^>/pick /p"
		fi > "$TODO"

		cat >> "$TODO" << EOF

# Rebase $SHORTUPSTREAM..$SHORTHEAD onto $SHORTONTO
#
# In the todo insn whenever you need to refer to a commit, in addition
# to the usual commit object name, you can use ':mark' syntax to refer
# to a commit previously marked with the 'mark' insn.
#
# Commands:
#  pick = use commit
#  edit = use commit, but stop for amending
#  squash = use commit, but meld into previous commit
#  mark :mark = mark the current HEAD for later reference
#  reset commit = reset HEAD to the commit
#  merge commit-M commit-P ... = redo merge commit-M with the
#         current HEAD and the parents commit-P
#  tag = reset tag to the current HEAD
#
# If you remove a line here THAT COMMIT WILL BE LOST.
# However, if you remove everything, the rebase will be aborted.
#
EOF

		has_action "$TODO" ||
			die_abort "Nothing to do"

		cp "$TODO" "$TODO".backup
		git_editor "$TODO" ||
			die "Could not execute editor"

		has_action "$TODO" ||
			die_abort "Nothing to do"

		output git checkout $ONTO && do_rest
		;;
	esac
	shift
done
