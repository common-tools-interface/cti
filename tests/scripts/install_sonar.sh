#!/bin/bash
SCRIPTS_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 &&pwd )"

cd $SCRIPTS_DIR/../coverage/

if test -d ./ss-linux ; then
    echo "Sonar scanner install is valid..."
    exit 0
fi
if ! test -f ./ss.zip ; then
    if ! curl https://binaries.sonarsource.com/Distribution/sonar-scanner-cli/sonar-scanner-cli-4.0.0.1744-linux.zip -o ss.zip ; then
    echo "Failed to download sonar scanner. Aborting..."
    exit 0
    fi
fi
unzip ss.zip
mv sonar-scanner-4.0.0.1744-linux ss-linux
cp $SCRIPTS_DIR/sonar-scanner.properties.default ./ss-linux/conf/sonar-scanner.properties
rm ss.zip
