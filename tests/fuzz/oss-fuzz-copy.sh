#!/usr/bin/env bash
set -e

# --------------------------------------------------------------------
# Copies all the corpus files, dict and options to the $OUT directory.
# This script is only used on oss-fuzz directly
# --------------------------------------------------------------------

fuzzerFiles=$(find $SRC/open62541/tests/fuzz/ -name "*.cc")

for F in $fuzzerFiles; do
	fuzzerName=$(basename $F .cc)

	if [ -d "$SRC/open62541/tests/fuzz/${fuzzerName}_corpus" ]; then
		zip -jr $OUT/${fuzzerName}_seed_corpus.zip $SRC/open62541/tests/fuzz/${fuzzerName}_corpus/
	fi
done

cp $SRC/open62541/tests/fuzz/*.dict $SRC/open62541/tests/fuzz/*.options $OUT/