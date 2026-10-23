#!/bin/sh
# Host tests for the portable mining core.
#   ./test/host/run.sh                 unit tests (offline)
#   ./test/host/run.sh pool [args...]  live test against a Stratum pool
set -e
cd "$(dirname "$0")/../.."
JSON=.pio/libdeps/esp32-headless/ArduinoJson/src
[ -d "$JSON" ] || pio pkg install -e esp32-headless >/dev/null
mkdir -p .pio/host
CORE="src/core/sha256.cpp src/core/work.cpp src/core/stratum.cpp src/core/address.cpp"
if [ "$1" = "pool" ]; then
    shift
    g++ -std=c++17 -O2 -Wall -pthread -Isrc -I"$JSON" test/host/pool_test.cpp $CORE -o .pio/host/pool_test
    exec .pio/host/pool_test "$@"
fi
g++ -std=c++17 -O2 -Wall -Isrc -I"$JSON" test/host/test_core.cpp $CORE -o .pio/host/test_core
.pio/host/test_core
