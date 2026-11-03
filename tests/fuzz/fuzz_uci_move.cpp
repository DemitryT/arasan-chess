// libFuzzer harness for Notation::value with UCI input, the path that
// "position ... moves ..." uses. Input: an optional FEN line, then
// whitespace-separated moves. Moves are applied the way protocol.cpp does
// (parse, legalMove, doMove), so long move lists are covered too.
#include "fuzz_common.h"
#include "notation.h"

#include <sstream>

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
    fuzzInit();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    std::string input(reinterpret_cast<const char *>(data), size);
    Board board; // start position
    const size_t eol = input.find('\n');
    if (eol != std::string::npos) {
        Board fromFen;
        if (BoardIO::readFEN(fromFen, input.substr(0, eol))) {
            board = fromFen;
        }
        input.erase(0, eol + 1);
    }
    std::istringstream moves(input);
    std::string text;
    while (moves >> text) {
        // relaxed parse first: must not crash even when the move is illegal
        (void)Notation::value(board, board.sideToMove(), Notation::InputFormat::UCI, text,
                              false);
        const Move m =
            Notation::value(board, board.sideToMove(), Notation::InputFormat::UCI, text);
        if (IsNull(m) || !legalMove(board, m)) {
            break;
        }
        board.doMove(m);
    }
    exercisePosition(board);
    return 0;
}
