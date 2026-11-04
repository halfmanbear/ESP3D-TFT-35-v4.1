#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/grbl-tests
compiler="${CXX:-c++}"
flags=(-std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -include cstdint -I main/target/cnc/grbl)
"$compiler" "${flags[@]}" tests/grbl/protocol_test.cpp main/target/cnc/grbl/grbl_protocol.cpp -o build/grbl-tests/protocol_test
build/grbl-tests/protocol_test
"$compiler" "${flags[@]}" tests/grbl/settings_test.cpp main/target/cnc/grbl/pendant_settings.cpp -o build/grbl-tests/settings_test
build/grbl-tests/settings_test
"$compiler" "${flags[@]}" -I tests/grbl/service/stubs tests/grbl/service/service_test.cpp main/target/cnc/grbl/esp3d_gcode_parser_service.cpp main/target/cnc/grbl/grbl_protocol.cpp -o build/grbl-tests/service_test
build/grbl-tests/service_test
# Real LVGL at 320x480, built with the sanitizers so use-after-free in widget callbacks is caught.
sanitize="-fsanitize=address,undefined -g -O1"
cmake -S tests/grbl/ui -B build/grbl-ui-test -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="$sanitize" -DCMAKE_CXX_FLAGS="$sanitize" > /dev/null
cmake --build build/grbl-ui-test -j 4 > build/grbl-ui-test/build.log || { tail -40 build/grbl-ui-test/build.log; exit 1; }
rm -rf build/grbl-ui-test/previews
mkdir -p build/grbl-ui-test/previews
build/grbl-ui-test/grbl_ui_test build/grbl-ui-test/previews
python3 tests/grbl/ui/ppm2png.py build/grbl-ui-test/previews/*.ppm
echo "Screenshots: build/grbl-ui-test/previews/*.png"
