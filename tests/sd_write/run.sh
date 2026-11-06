#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
TMP=${TMPDIR:-/tmp}/cuzebox-sd-write-test-$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP/include/SDL2"
printf '%s\n' '#pragma once' > "$TMP/include/SDL2/SDL.h"
cc -std=gnu99 -Wall -Wextra -Werror -DHEADLESS=1 -DFLAG_NOCONSOLE=1 \
  -I"$TMP/include" -I"$ROOT" \
  "$ROOT/tests/sd_write/sd_write_regression.c" "$ROOT/filesys.c" "$ROOT/cu_vfat.c" \
  -o "$TMP/sd_write_regression"
"$TMP/sd_write_regression"

cc -std=gnu99 -Wall -Wextra -Werror -DHEADLESS=1 -DFLAG_NOCONSOLE=1 \
  -I"$TMP/include" -I"$ROOT" \
  "$ROOT/tests/sd_write/nested_regression.c" "$ROOT/filesys.c" "$ROOT/cu_vfat.c" \
  -o "$TMP/nested_regression"
"$TMP/nested_regression"

cc -std=gnu99 -Wall -Wextra -Werror -DHEADLESS=1 -DFLAG_NOCONSOLE=1 \
  -I"$TMP/include" -I"$ROOT" \
  "$ROOT/tests/sd_write/appended_uze_regression.c" "$ROOT/filesys.c" "$ROOT/cu_vfat.c" \
  -o "$TMP/appended_uze_regression"
"$TMP/appended_uze_regression"

cc -std=gnu99 -Wall -Wextra -Werror -DHEADLESS=1 -DFLAG_NOCONSOLE=1 \
  -I"$TMP/include" -I"$ROOT" \
  "$ROOT/tests/sd_write/uzedisk_regression.c" "$ROOT/filesys.c" "$ROOT/cu_vfat.c" \
  -o "$TMP/uzedisk_regression"
"$TMP/uzedisk_regression"
