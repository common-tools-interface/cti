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
set -ex

echo "Creating output directory structure..."

OUTPUT_DIR="cpedocs_cti"
mkdir -p "$OUTPUT_DIR/man"

echo "Collecting files..."

# Collect index
cp ./cpedocs_index_chunk.md "$OUTPUT_DIR"

# Collect man pages
# Intentionally using a for loop instead of `cp ./man/*.md` for a more verbose output
for f in ../man/*.md; do
  cp "$f" "$OUTPUT_DIR/man/"
done

# We swap out some manpage headings like "NAME" to make the search experience in sphinx a little better.
confirm_and_replace() {
	# Relying on the fact that set -e was called at the top of the script for error checking
	grep "$1" "$3"
	sed -i "s/$1/$2/" "$3"
}

confirm_and_replace "# NAME" "# CTI User Reference" "$OUTPUT_DIR/man/cti.1.md"
confirm_and_replace "# NAME" "# CTI Developer Reference" "$OUTPUT_DIR/man/cti.3.md"

echo "Done. Output directory is $OUTPUT_DIR."
echo "Now copy $OUTPUT_DIR to /CPEDocs/doc/debugging-tools."
