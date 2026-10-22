#!/bin/sh
# Run the decompilation regression tests.
#   ./tools/test.sh          - differential tests + CasioEmu cross-check
#   ./tools/test.sh --quick  - differential tests only
set -e
cd "$(dirname "$0")/.."

CC=${CC:-gcc}
CFLAGS="-std=c11 -Wall -Wextra -O2"

mkdir -p build

echo "== differential tests =="
$CC $CFLAGS -o build/diff_all \
    tests/main.c tests/harness.c \
    tests/t_string.c tests/t_mem.c tests/t_display.c tests/t_font.c tests/t_print.c tests/t_keyboard.c tests/t_edit.c tests/t_emu.c \
    src/string.c src/mem.c src/display.c src/font.c src/print.c src/keyboard.c src/edit.c \
    emu/nx8.c
./build/diff_all resource/rom.bin

$CC $CFLAGS -o build/xcheck tests/xcheck.c emu/nx8.c
./build/xcheck resource/rom.bin > build/xcheck_mine.txt

if [ "$1" = "--quick" ]; then
    exit 0
fi

echo "== CasioEmu cross-check =="
if [ ! -x machine/emulator/build/emulator ]; then
    echo "skipped: machine/emulator/build/emulator not built"
    exit 0
fi
( cd machine && SDL_VIDEODRIVER=dummy timeout -s KILL 120 \
    emulator/build/emulator models/fx580vnx_emu \
    script=../tools/xcheck.lua ) 2>&1 | grep "^CASE " > build/xcheck_ref.txt
if diff -u build/xcheck_mine.txt build/xcheck_ref.txt; then
    echo "cross-check: OK ($(wc -l < build/xcheck_ref.txt) cases)"
else
    echo "cross-check: MISMATCH"
    exit 1
fi
