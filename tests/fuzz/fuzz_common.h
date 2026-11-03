// Shared setup for the libFuzzer harnesses. See README.md in this folder.
#ifndef _FUZZ_COMMON_H
#define _FUZZ_COMMON_H

#include "attacks.h"
#include "bhash.h"
#include "bitutil.h"
#include "board.h"
#include "boardio.h"
#include "constant.h"
#include "legal.h"
#include "movegen.h"

#include <cstdint>
#include <cstdlib>
#include <string>

// One-time engine table setup (no NNUE network needed).
static inline void fuzzInit() {
    BitUtils::init();
    Board::init();
    Attacks::init();
}

// Exercise a position the parser accepted: its hash must match a fresh
// computation, and every legal move must make and unmake cleanly. This
// catches accepted-but-impossible positions that would crash later.
static inline void exercisePosition(Board &board) {
    if (board.hashCode() != BoardHash::hashCode(board)) {
        abort();
    }
    std::string fen;
    BoardIO::writeFEN(board, fen, 0);
    MoveGenerator mg(board);
    Move moves[Constants::MaxMoves];
    const unsigned n = mg.generateAllMoves(moves, 0);
    const hash_t before = board.hashCode();
    for (unsigned i = 0; i < n; i++) {
        const BoardState state = board.state;
        board.doMove(moves[i]);
        (void)board.repCount(1);
        (void)board.isLegalDraw();
        board.undoMove(moves[i], state);
        if (board.hashCode() != before) {
            abort();
        }
    }
}

#endif
