all: clean-local @CHECKSUM_HEADER_FILE@

CS_FILES = @CHECKSUM_FILES@
CS_TARGETS = $(patsubst %, %.sha1sum, $(CS_FILES))

clean-local:
	rm -f @CHECKSUM_HEADER_FILE@ @CHECKSUM_HEADER_FILE@.sums

@CHECKSUM_HEADER_FILE@: clean-local @CHECKSUM_HEADER_FILE@.sums
	cp @CHECKSUM_HEADER_FILE@.in @CHECKSUM_HEADER_FILE@
	cat @CHECKSUM_HEADER_FILE@.sums >> @CHECKSUM_HEADER_FILE@
	printf "\n#endif\n" >> @CHECKSUM_HEADER_FILE@

@CHECKSUM_HEADER_FILE@.sums: $(CS_TARGETS)

%.sha1sum: %
	printf "#define %s_CHECKSUM \"%s\"\n" \
	$(subst .,_,$(notdir $<)) \
	$(firstword $(shell @CHECKSUM_PROG@ $<)) \
	>> @CHECKSUM_HEADER_FILE@.sums
