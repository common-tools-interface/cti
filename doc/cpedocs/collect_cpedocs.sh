#!/usr/bin/bash
#
# This script is for CPEDocs: https://github.hpe.com/hpe/CPEDocs
#
# This script will collect documentation files that live in this repository and create a
# directory containing them. The newly created directory will have a structure compatible
# with the CPEDocs project and can be copied directly into CPEDocs/docs/debugging-tools,
# where the build system in CPEDocs will accept it.
#
# This script should be run from the directory it lives in.

# Stop immediately if anything unexpected happens
set -e

echo "Creating output directory structure..."

OUTPUT_DIR="cpedocs_cti"
mkdir -p "$OUTPUT_DIR/man"
echo "  mkdir $OUTPUT_DIR/man"

echo "Collecting files..."

# Collect index
echo "  ./cpedocs_index_chunk.md -> $OUTPUT_DIR/"
cp ./cpedocs_index_chunk.md "$OUTPUT_DIR"

# Collect man pages
# Intentionally using a for loop instead of `cp ./man/*.md` for a more verbose output
for f in ../man/*.md; do
  echo "  $f -> $OUTPUT_DIR/man/"
  cp "$f" "$OUTPUT_DIR/man/"
done

echo "Done. Output directory is $OUTPUT_DIR."
echo "Now copy $OUTPUT_DIR to /CPEDocs/doc/debugging-tools."
