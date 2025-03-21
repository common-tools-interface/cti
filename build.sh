#!/bin/bash

set -e

export PRODUCT=${PRODUCT:-pe-base}
export BUILD_TARGETS=""
export LOCAL_BUILD="yes"

echo -e -n "Running as: "
id

setup_env() {
  if [[ -z ${VIRTUAL_ENV} ]] && [[ ! -d dstlibvenv ]]; then
    pip3 install virtualenv pip --trusted-host https://arti.hpc.amslabs.hpecorp.net \
      --extra-index-url https://arti.hpc.amslabs.hpecorp.net:443/artifactory/dst-pip-master-local \
      --index-url https://arti.hpc.amslabs.hpecorp.net:443/artifactory/api/pypi/pypi-remote/simple

    python3 -m venv dstlibvenv
    source dstlibvenv/bin/activate

    pip3 install craydstlib --trusted-host https://arti.hpc.amslabs.hpecorp.net \
      --extra-index-url https://arti.hpc.amslabs.hpecorp.net:443/artifactory/dst-pip-master-local \
      --index-url https://arti.hpc.amslabs.hpecorp.net:443/artifactory/api/pypi/pypi-remote/simple
  else
    source dstlibvenv/bin/activate
  fi
  dst-rpm --version
  
}

build() {
  command -v podman && podman version
  command -v docker && docker version

  set -x
  # Note BUILD_METADATA will be available inside the container since BUILD_METADATA is include in the env_vars file
  export BUILD_METADATA=$(dst-common get-build-metadata)
  export DEFAULT_BRANCH=$(git remote show origin | sed -n '/HEAD branch/s/.*: //p')

  #   LOCAL_BUILD is either "yes" or empty
  if [ -n "$LOCAL_BUILD" ]; then
    dst-rpm build --product "${PRODUCT}" --build-list "${BUILD_TARGETS}" --main-branch ${DEFAULT_BRANCH}
  else
    dst-rpm build --product "${PRODUCT}" --build-list "${BUILD_TARGETS}"  --main-branch ${DEFAULT_BRANCH} --not-local --signer-login dst-de

    QUALITY_STREAM=`dst-common get-quality-stream --master-branch "${DEFAULT_BRANCH}"`
    top_dir=$PWD
    for workspace in workspaces/*/RPMS; do
      cd ${top_dir}/${workspace}
      for yaml in `find . -name \*.rpm.yaml`; do
        dir=$(dirname ${yaml})
        filename=$(basename ${yaml})
        dst-artifactory upload --repo "${PRODUCT}-misc-${QUALITY_STREAM}-local" --subdir ${dir#./} --files ${dir}/${filename}
      done
    done
    cd ${top_dir}
  fi
  set +x
}

usage() {
    cat <<EOF
Usage: $0 [options]

-h  display this message
-n  flag as non-local build
-t  build targets (comma separated list of targets from container_def.yaml)
EOF
}

main() {
  setup_env
  build 
}

while [ -n "$1" ]; do
  case "$1" in
      -h | --help)
        usage
        shift
        exit 0
        ;;
      -t)
        export BUILD_TARGETS=$2
        shift
        shift
        ;;
      -n | --not-local | -L)
        LOCAL_BUILD=""
        shift
        ;;
      *)
        if [ "$1" != "${1#-}" ] ; then
          echo "Unknown option: $1"
          usage
          exit 1 
        else
          break
        fi
        ;;
  esac
done

main
