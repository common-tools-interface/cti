#/bin/bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

# Detect libflux flags with pkg-config
if [ -z "$1" ]; then
	FLUX_CORE_PC="$(dirname $(which flux))/../lib64/pkgconfig/flux-core.pc"
else
	FLUX_CORE_PC="$1"
fi
FLUX_CFLAGS=$(pkg-config --cflags $FLUX_CORE_PC)

# Generate compatibility check header
python3 generate_libflux.py --compiler-check LibFlux.hpp.in > LibFluxCompat.hpp

# Try to compile compatibility check header
pushd $SCRIPT_DIR
g++ -x c++ --std=c++17 $FLUX_CFLAGS -o /dev/null - <<EOF
#include "LibFluxCompat.hpp"
int main(int argc, char** argv) { return 0; }
EOF
popd

echo "All functions compatible with this Flux installation"
