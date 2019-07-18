#!/bin/bash
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
cp ./ss_props.txt ./ss-linux/conf/sonar-scanner.properties
rm ss.zip
