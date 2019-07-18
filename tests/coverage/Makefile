SRC := $(PWD)/../../src/
GCNOS := $(wildcard ../../src/frontend/cti_transfer/.libs/*.gcno)
GCDAS := $(GCNOS:.gcno=.gcda)

ifeq ($(GCNOS),)
    echo "Please use --enable-code-coverage in build.sh and rebuild"
    exit 1
endif

.PHONY: coverage
coverage: coverage.xml

coverage.xml: $(GCDAS)
	cd ../unit && ./unit_tests
	cd ../function && ./build_run.sh
	cd ../coverage && ./build_coverage_report.sh

sonar-scan:
	./ss-linux/bin/sonar-scanner
	@echo
	@echo View SonarQube results at
	@echo https://sonar.us.cray.com/dashboard?id=cti

clean:
	rm coverage.xml
