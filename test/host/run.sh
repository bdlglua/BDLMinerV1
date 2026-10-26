#!/bin/sh
# Host tests for the portable mining core.
#   ./test/host/run.sh                 unit tests (offline)
#   ./test/host/run.sh pool [args...]  live test against a Stratum pool
set -e
cd "$(dirname "$0")/../.."
JSON=.pio/libdeps/esp32-headless/ArduinoJson/src
[ -d "$JSON" ] || pio pkg install -e esp32-headless >/dev/null
mkdir -p .pio/host
CORE="src/core/sha256.cpp src/core/work.cpp src/core/stratum.cpp src/core/address.cpp src/core/fill.cpp"
if [ "$1" = "pool" ]; then
    shift
    g++ -std=c++17 -O2 -Wall -pthread -Isrc -I"$JSON" test/host/pool_test.cpp $CORE -o .pio/host/pool_test
    exec .pio/host/pool_test "$@"
fi
g++ -std=c++17 -O2 -Wall -Isrc -I"$JSON" test/host/test_core.cpp $CORE -o .pio/host/test_core
.pio/host/test_core
python3 test/host/test_hw_resume.py
g++ -std=c++17 -O2 -Wall -Isrc -I"$JSON" test/host/test_line_buffer.cpp $CORE -o .pio/host/test_line_buffer
.pio/host/test_line_buffer
g++ -std=c++17 -O2 -Wall -Isrc -I"$JSON" test/host/test_web_validation.cpp -o .pio/host/test_web_validation
.pio/host/test_web_validation
g++ -std=c++17 -O2 -Wall -Wno-unused-variable -ffunction-sections -fdata-sections \
    -Itest/host/stubs/miner -Isrc test/host/test_miner_queue.cpp \
    src/core/sha256.cpp src/core/work.cpp -Wl,--gc-sections -o .pio/host/test_miner_queue
.pio/host/test_miner_queue
g++ -std=c++17 -O2 -Wall -pthread -Isrc test/host/test_mining_safety.cpp -o .pio/host/test_mining_safety
.pio/host/test_mining_safety
g++ -std=c++17 -O2 -Wall -Itest/host/stubs/miner -Isrc -I"$JSON" \
    test/host/test_settings_apply.cpp -o .pio/host/test_settings_apply
.pio/host/test_settings_apply
g++ -std=c++17 -O2 -Wall -Itest/host/stubs/miner -Isrc -I"$JSON" test/host/test_settings_store.cpp -o .pio/host/test_settings_store
.pio/host/test_settings_store
python3 test/host/test_settings_persistence.py
python3 test/host/test_web_upload.py
python3 test/host/test_ghupdate.py
