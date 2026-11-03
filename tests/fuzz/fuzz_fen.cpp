// libFuzzer harness for BoardIO::readFEN. Any input either is rejected or
// yields a position that survives exercisePosition().
#include "fuzz_common.h"

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
    fuzzInit();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    const std::string fen(reinterpret_cast<const char *>(data), size);
    Board board;
    if (BoardIO::readFEN(board, fen)) {
        exercisePosition(board);
    }
    return 0;
}
