#!/bin/sh
# Compile the firmware stretch C (tools/stretch) plus lab.c to tools/lab/lab.wasm.
# Needs Homebrew lld for wasm-ld: brew install lld
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
STRETCH="$HERE/../stretch"
WASM_LD=$(brew --prefix lld)/bin/wasm-ld
OBJ=$(mktemp -d)
for f in "$HERE/lab.c" "$STRETCH/capi.c" "$STRETCH/smooth.c" "$STRETCH/vari.c" "$STRETCH/beats.c" "$STRETCH/deluge.c" "$STRETCH/granular.c"; do
    clang --target=wasm32 -O2 -ffreestanding -fno-builtin -nostdlib -I "$STRETCH" \
        -c -o "$OBJ/$(basename "$f" .c).o" "$f"
done
"$WASM_LD" --no-entry --export-dynamic --strip-all -o "$HERE/lab.wasm" "$OBJ"/*.o
rm -rf "$OBJ"
echo "wrote $HERE/lab.wasm ($(wc -c < "$HERE/lab.wasm") bytes)"
