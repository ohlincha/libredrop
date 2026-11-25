#!/bin/sh
# Analyses every real recording in testdata/real (not in git: see testdata/README.md) and prints each drop's
# summary. Compare with testdata/expected.txt after a change to the analysis:
#   tools/regress_videos.sh > /tmp/now.txt; diff testdata/expected.txt /tmp/now.txt
# CLI: LIBREDROP_CLI (default: LibreDropCLI from ~/opt/build-libredrop, else on PATH). Scale 94.23 px/mm as recorded.
cd "$(dirname "$0")/.." || exit 1
CLI=${LIBREDROP_CLI:-$HOME/opt/build-libredrop/LibreDropCLI}
[ -x "$CLI" ] || CLI=LibreDropCLI
for f in testdata/real/*.mkv; do
    [ -e "$f" ] || { echo "no recordings in testdata/real"; exit 1; }
    echo "== $(basename "$f")"
    "$CLI" video "$f" --scale 94.23 --step 2 2>&1 | grep -v 'frames$' | grep -E '^drop|measured|angle |after contact|free from'
done
