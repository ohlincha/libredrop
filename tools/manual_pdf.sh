#!/bin/sh
# Makes docs/LibreDrop-Manual.pdf from docs/MANUAL.md (needs pandoc and google-chrome or chromium).
set -e
cd "$(dirname "$0")/../docs"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
pandoc MANUAL.md -s --embed-resources --metadata title="LibreDrop manual" -c manual.css -o "$tmp/manual.html"
chrome=$(command -v google-chrome || command -v chromium || command -v chromium-browser)
"$chrome" --headless --disable-gpu --no-pdf-header-footer --print-to-pdf="$PWD/LibreDrop-Manual.pdf" "file://$tmp/manual.html" 2>/dev/null
echo "docs/LibreDrop-Manual.pdf: $(pdfinfo LibreDrop-Manual.pdf 2>/dev/null | sed -n 's/^Pages: *//p') pages"
