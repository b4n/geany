#!/bin/bash

top_builddir="${top_builddir:-../..}"

CONFDIR=$(mktemp -d)
RUN="${top_builddir}/src/geany -c $CONFDIR -P -g"

trap '# cleanup
rm -rf $CONFDIR
rm -f test.*.tags' EXIT

for test in `ls -1 tests`; do
	echo "testing $test..."
	tagfile=test.${test##*.}.tags
	$RUN "$tagfile" "tests/$test" || exit 1
	diff "$tagfile" "results/$test.tags" || exit 2
done
