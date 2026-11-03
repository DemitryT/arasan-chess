// Copyright 1997, 2008, 2021, 2024-2025 by Jon Dart.  All Rights Reserved.

#include "boardio.h"
#include "attacks.h"
#include "bhash.h"

#include <cassert>
#include <sstream>

int BoardIO::readFEN(Board &board, const std::string &buf) {
    // we modify only a temporary board, until all parsing is
    // done and the FEN is validated.
    Board temp;
    for (int i = 0; i < 64; i++) {
        temp.contents[i] = EmptyPiece;
    }

    char c;
    std::string::const_iterator bp = buf.begin();
    size_t first = buf.find_first_not_of(" \t");
    if (first != std::string::npos) {
        bp += first;
    }
    for (int line = 0; line < 8; line++) {
        int sqval = 56 - line * 8;
        const int rankEnd = sqval + 8;
        while (bp != buf.end() && (c = *bp) != ' ' && c != '/') {
            Piece piece;
            if (isdigit(c)) {
                sqval += (*bp - '0');
                if (sqval > rankEnd) {
                    return 0;
                }
                bp++;
            } else if (isalnum(c)) {
                if (sqval >= rankEnd) {
                    return 0;
                }
                switch (c) {
                case 'p':
                    piece = BlackPawn;
                    break;
                case 'n':
                    piece = BlackKnight;
                    break;
                case 'b':
                    piece = BlackBishop;
                    break;
                case 'r':
                    piece = BlackRook;
                    break;
                case 'q':
                    piece = BlackQueen;
                    break;
                case 'k':
                    piece = BlackKing;
                    break;
                case 'P':
                    piece = WhitePawn;
                    break;
                case 'N':
                    piece = WhiteKnight;
                    break;
                case 'B':
                    piece = WhiteBishop;
                    break;
                case 'R':
                    piece = WhiteRook;
                    break;
                case 'Q':
                    piece = WhiteQueen;
                    break;
                case 'K':
                    piece = WhiteKing;
                    break;
                default:
                    return 0;
                }
                temp.contents[sqval] = piece;
                sqval++;
                bp++;
            } else // not a letter or a digit
            {
                return 0;
            }
        }
        if (bp == buf.end() || sqval != rankEnd) {
            return 0;
        }
        if (c == '/')
            ++bp; // skip delimiter
    }
    while (bp != buf.end() && isspace(*bp))
        bp++;
    if (bp == buf.end())
        return 0;
    if (toupper(*bp) == 'W')
        temp.side = White;
    else if (toupper(*bp) == 'B')
        temp.side = Black;
    else {
        return 0;
    }
    bp++;
    while (bp != buf.end() && isspace(*bp))
        bp++;
    if (bp == buf.end())
        return 0;
    c = *bp;
    if (c == '-') {
        temp.state.castleStatus[White] = temp.state.castleStatus[Black] = CantCastleEitherSide;
        bp++;
    } else {
        int k = 0;
        for (; bp != buf.end() && !isspace(*bp); bp++) {
            int flag;
            if (*bp == 'K')
                flag = 1;
            else if (*bp == 'Q')
                flag = 2;
            else if (*bp == 'k')
                flag = 4;
            else if (*bp == 'q')
                flag = 8;
            else {
                return 0;
            }
            // Reject repeated letters (e.g. "qq"): they would push k past 15
            // and index past the end of vals.
            if (k & flag) {
                return 0;
            }
            k |= flag;
        }
        static const CastleType vals[4] = {CantCastleEitherSide, CanCastleKSide, CanCastleQSide,
                                           CanCastleEitherSide};
        temp.state.castleStatus[White] = vals[k % 4];
        temp.state.castleStatus[Black] = vals[k / 4];
    }
    temp.setSecondaryVars();
    while (bp != buf.end() && isspace(*bp))
        bp++;
    if (bp == buf.end())
        return 0;
    c = *bp;
    if (c == '-') {
        temp.state.enPassantSq = InvalidSquare;
    } else if (isalpha(c)) {
        char sqbuf[2];
        sqbuf[0] = *bp++;
        if (bp == buf.end())
            return 0;
        sqbuf[1] = *bp++;
        Square epsq = SquareValue(sqbuf);
        // The e.p. square must be on the 6th rank from the side to move's
        // perspective (rank 6 if White moves, rank 3 if Black moves).
        // Otherwise ep_candidate below can fall off the board.
        if (epsq == InvalidSquare || Rank(epsq, temp.sideToMove()) != 6) {
            return 0;
        }
        temp.state.enPassantSq = InvalidSquare;
        Square ep_candidate = SquareValue(sqbuf) - (8 * Direction[temp.sideToMove()]);
        // The opponent's pawn must be on the square it just double-moved to,
        // with the square it passed and the one it came from empty. Otherwise
        // an e.p. capture would remove some other piece as if it were a pawn.
        const Square ep_origin = epsq + (8 * Direction[temp.sideToMove()]);
        if (temp.contents[ep_candidate] != MakePiece(Pawn, temp.oppositeSide()) ||
            temp.contents[epsq] != EmptyPiece || temp.contents[ep_origin] != EmptyPiece) {
            return 0;
        }
        // only actually set the e.p. square on the board if an en-passant capture
        // is truly possible:
        if (Attacks::ep_mask[File(ep_candidate) - 1][(int)temp.oppositeSide()] &
            temp.pawn_bits[temp.sideToMove()]) {
            temp.state.enPassantSq = ep_candidate;
            // re-calc hash code since ep has changed
            temp.state.hashCode = BoardHash::hashCode(temp);
        }
    } else {
        return 0;
    }
    temp.repList[temp.state.moveCount++] = temp.hashCode();
    if (temp.kingPos[White] == InvalidSquare || temp.kingPos[Black] == InvalidSquare) {
        return 0;
    }
    // Reject positions that can't occur in a game and that the move
    // generator doesn't expect: more than one king per side, pawns on the
    // first or last rank, or the side not to move in check (which lets the
    // side to move "capture" the king, corrupting kingPos). Also more men
    // than a game allows: Material keeps each piece count in 4 bits, so e.g.
    // 16 pawns overflow into the knight count.
    int counts[2][8] = {{0}, {0}};
    for (Square sq = 0; sq < 64; sq++) {
        const Piece p = temp.contents[sq];
        if (p == EmptyPiece) continue;
        ++counts[PieceColor(p)][TypeOfPiece(p)];
        if (TypeOfPiece(p) == Pawn && (sq < 8 || sq >= 56)) {
            return 0;
        }
    }
    for (ColorType side : colors) {
        const int *n = counts[side];
        // pieces beyond the starting set must have come from promotions
        const int promoted = std::max(0, n[Queen] - 1) + std::max(0, n[Rook] - 2) +
                             std::max(0, n[Bishop] - 2) + std::max(0, n[Knight] - 2);
        if (n[King] != 1 || n[Pawn] > 8 || promoted > 8 - n[Pawn]) {
            return 0;
        }
    }
    if (temp.anyAttacks(temp.kingSquare(temp.oppositeSide()), temp.sideToMove())) {
        return 0;
    }
    // Perform some sanity checking on castling status
    for (ColorType stm : colors) {
        if (temp.canCastleKSide(stm) && !temp.sanityCheckKSideCastling(stm)) {
            return 0;
        }
        if (temp.canCastleQSide(stm) && !temp.sanityCheckQSideCastling(stm)) {
            return 0;
        }
    }
    board = temp;
    return 1;
}

void BoardIO::writeFEN(const Board &board, std::ostream &o, bool addMoveInfo) {
    // write out the board in Forsythe-Edwards (FEN) notation.
    for (int i = 1; i <= 8; i++) {
        int j = 1;
        while (j <= 8) {
            int n = 0;
            Square sq;
            Piece p;
            do {
                sq = MakeSquare(j, i, Black);
                p = board.contents[sq];
                if (p != EmptyPiece)
                    break;
                ++j;
                ++n;
            } while (j <= 8);
            if (n)
                o << (char)(n + '0');
            if (p != EmptyPiece) {
                char img = PieceImage(TypeOfPiece(p));
                if (PieceColor(p) == Black)
                    img = tolower(img);
                o << img;
                j++;
            }
        }
        if (i != 8)
            o << '/';
    }
    if (board.sideToMove() == White)
        o << " w";
    else
        o << " b";

    // used in I/O of castling data:
    constexpr bool kcastle[4] = {true, true, false, false};
    constexpr bool qcastle[4] = {true, false, true, false};

    int wcs = static_cast<int>(board.castleStatus(White));
    int bcs = static_cast<int>(board.castleStatus(Black));
    o << ' ';
    assert(wcs >= 0 && wcs <= 3 && bcs >=0 && wcs <=3);
    if (wcs == 3 && bcs == 3) {
        o << '-';
    } else {
        if (kcastle[wcs])
            o << 'K';
        if (qcastle[wcs])
            o << 'Q';
        if (kcastle[bcs])
            o << 'k';
        if (qcastle[bcs])
            o << 'q';
    }
    o << ' ';
    Square epsq = board.enPassantSq();
    if (epsq == InvalidSquare) {
        o << '-';
    } else {
        // FEN stores the destination square for an en passant capture;
        // we store the location of the capturable pawn.
        Square target = (board.sideToMove() == White) ? epsq + 8 : epsq - 8;
        o << FileImage(target) << RankImage(target);
    }
    if (addMoveInfo) {
        // FEN is supposed to include the halfmove and fullmove numbers,
        // but these are attributes of the game - they are not stored in
        // the board.
        o << " 0 1";
    }
}

void BoardIO::writeFEN(const Board &board, std::string &o, bool addMoveInfo) {
    std::stringstream s;
    writeFEN(board, s, addMoveInfo);
    o = s.str();
}
