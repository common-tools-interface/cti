#/bin/bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

FLUX_CORE_PC="$(dirname $(which flux))/../lib/pkgconfig/flux-core.pc"
FLUX_CFLAGS=$(pkg-config --cflags $FLUX_CORE_PC)

pushd $SCRIPT_DIR
g++ -x c++ --std=c++17 $FLUX_CFLAGS -o /dev/null - <<EOF
#include "LibFluxCompat.hpp"
int main(int argc, char** argv) { return 0; }
EOF
popd

echo "All functions compatible with this Flux installation"
