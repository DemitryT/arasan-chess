#!/bin/sh
# Build the libFuzzer harnesses with ASan + UBSan. Needs a clang that ships
# libFuzzer (Apple's doesn't; on macOS: brew install llvm).
#
#   tests/fuzz/build.sh            # builds tests/fuzz/out/fuzz_fen, fuzz_uci_move
#   tests/fuzz/out/fuzz_fen -max_total_time=300 tests/fuzz/corpus/fen
set -e
cd "$(dirname "$0")"
if [ -z "$CXX" ] && command -v brew >/dev/null; then
    CXX="$(brew --prefix llvm)/bin/clang++"
fi
CXX=${CXX:-clang++}
SRC=../../src
# Target this machine's architecture (Homebrew's clang may itself run
# under Rosetta) and the macOS SDK.
TARGET="-arch $(uname -m)"
if command -v xcrun >/dev/null; then TARGET="$TARGET -isysroot $(xcrun --show-sdk-path)"; fi
OUT=out
mkdir -p "$OUT/obj"

SOURCES="globals.cpp board.cpp boardio.cpp material.cpp chess.cpp attacks.cpp
bitutil.cpp chessio.cpp epdrec.cpp bhash.cpp scoring.cpp see.cpp movearr.cpp
notation.cpp options.cpp bitprobe.cpp bookread.cpp bookwrit.cpp search.cpp
searchc.cpp learn.cpp movegen.cpp hash.cpp calctime.cpp eco.cpp legal.cpp
stats.cpp threadp.cpp threadc.cpp evaluate.cpp nnue/features/ppfeature.cpp"

SIMD=""
if [ "$(uname -m)" = "arm64" ]; then SIMD="-DSIMD -DNEON"; fi
# Asserts stay on (no -DNDEBUG): an assert firing is a finding too.
FLAGS="$TARGET -std=c++17 -g -O1 -fno-omit-frame-pointer -D_MAC -D_64BIT -DUSE_INTRINSICS
-DARASAN_EMBEDDED $SIMD -I$SRC -I$SRC/nnue -DNETWORK=arasanv9-20261023.nnue"

OBJS=""
for f in $SOURCES; do
    o="$OUT/obj/$(basename "${f%.cpp}").o"
    $CXX $FLAGS -fsanitize=fuzzer-no-link,address,undefined -c "$SRC/$f" -o "$o"
    OBJS="$OBJS $o"
done
for h in fuzz_fen fuzz_uci_move; do
    $CXX $FLAGS -fsanitize=fuzzer,address,undefined "$h.cpp" $OBJS -o "$OUT/$h"
done
echo "built: $OUT/fuzz_fen $OUT/fuzz_uci_move"
