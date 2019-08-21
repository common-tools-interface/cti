all: clean-local @CHECKSUM_HEADER_FILE@

CS_FILE_NAME_PAIRS = @CHECKSUM_FILES@
CS_FILES = $(basename $(subst @,.,$(CS_FILE_NAME_PAIRS)))
CS_TARGETS = $(patsubst %, %_CHECKSUM, $(CS_FILE_NAME_PAIRS))
#.PHONY: $(CS_TARGETS)

clean-local:
	rm -f @CHECKSUM_HEADER_FILE@ @CHECKSUM_HEADER_FILE@.sums

@CHECKSUM_HEADER_FILE@: clean-local @CHECKSUM_HEADER_FILE@.sums
	cp @CHECKSUM_HEADER_FILE@.in @CHECKSUM_HEADER_FILE@
	cat @CHECKSUM_HEADER_FILE@.sums >> @CHECKSUM_HEADER_FILE@
	printf "\n#endif\n" >> @CHECKSUM_HEADER_FILE@

@CHECKSUM_HEADER_FILE@.sums: $(CS_FILES) $(CS_TARGETS)

%_CHECKSUM:
	printf "#define %s \"%s\"\n" \
	$(subst .,,$(suffix $(subst @,.,$@))) \
	$(firstword $(shell @CHECKSUM_PROG@ $(basename $(subst @,.,$@)))) \
	>> @CHECKSUM_HEADER_FILE@.sums
