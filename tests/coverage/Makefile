SRC := $(PWD)/../../src/
GCNOS := $(wildcard ../../src/frontend/cti_transfer/.libs/*.gcno)

ifeq ($(GCNOS),)
    echo "Please use --enable-code-coverage in build.sh and rebuild"
    exit 1
endif

.PHONY: coverage
coverage:
	../scripts/create_gcov_report.sh ../scripts

sonar-scan:
	sonar-scanner
	@echo
	@echo View SonarQube results at
	@echo https://sonar.us.cray.com/dashboard?id=cti

clean:
	rm -rf ../gcov_report/*
