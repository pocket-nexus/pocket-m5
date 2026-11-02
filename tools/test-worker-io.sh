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
  -I programs/m5stack-tab5/components/pocket_system/src tests/install-uri.c \
  programs/m5stack-tab5/components/pocket_system/src/install_uri.c \
  -o "$validation_dir/install-uri"
"$validation_dir/install-uri"
bun test ./tests/sensors.test.ts ./tests/rtc-setting.test.ts ./tests/pocket-shell-clock.test.ts ./tests/unit-swap.test.ts
