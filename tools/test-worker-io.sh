#!/bin/sh
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"
validation_dir=${1:-.pocket-build/validation/worker/host}
mkdir -p "$validation_dir"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I platforms/esp-idf/pocket_io_esp/src tests/io-protocol.c \
  platforms/esp-idf/pocket_io_esp/src/protocol.c -o "$validation_dir/io-protocol"
"$validation_dir/io-protocol"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I platforms/esp-idf/pocket_http_esp/src tests/http-record.c \
  platforms/esp-idf/pocket_http_esp/src/record.c -o "$validation_dir/http-record"
"$validation_dir/http-record"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I platforms/esp-idf/pocket_app_host/src tests/worker-package.c \
  platforms/esp-idf/pocket_app_host/src/package_info.c -o "$validation_dir/worker-package"
"$validation_dir/worker-package"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I tests/esp-dpi-stubs \
  -I platforms/esp-idf/pocket_display_esp_dpi_flip/include \
  -I "${POCKETJS_ROOT:-third_party/pocketjs}/contracts/generated" \
  tests/display-dpi.c \
  platforms/esp-idf/pocket_display_esp_dpi_flip/src/display.c \
  -o "$validation_dir/display-dpi"
"$validation_dir/display-dpi"
bun test tests/*.test.ts
