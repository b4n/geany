#!/bin/bash

top_builddir="${top_builddir:-../..}"
srcdir="${srcdir:-.}"

CONFDIR=$(mktemp -d)
RUN="${top_builddir}/src/geany -c $CONFDIR -P -g"

trap '# cleanup
rm -rf $CONFDIR
rm -f test.*.tags' EXIT

tests_dir=$srcdir/tests
results_dir=$srcdir/results
for test in `ls -1 $tests_dir`; do
	echo "testing $test..."
	tagfile=test.${test##*.}.tags
	$RUN "$tagfile" "$tests_dir/$test" || exit 1
	diff "$tagfile" "$results_dir/$test.tags" || exit 2
done
