# Arasan in Rooky

This `rooky` branch of [DemitryT/arasan-chess](https://github.com/DemitryT/arasan-chess)
(forked from [jdart1/arasan-chess](https://github.com/jdart1/arasan-chess),
base commit `84a67879b4b6`) is the bot engine for Rooky, an iOS chess app.
`master` stays identical to upstream. Arasan is MIT licensed (see `LICENSE`);
the bundled network file is covered by the same license.

## What we changed, and why

Each change is its own commit, so upstream merges stay easy.

| Commit | Change | Why |
| --- | --- | --- |
| readFEN: reject repeated castling letters | A castling field like `qq` is rejected. | `k` overflowed, `vals[k / 4]` read past a 4-entry array, and the garbage was used to index the castle hash tables. |
| readFEN: validate the en passant square's rank | The e.p. square must be on rank 6 (White to move) or rank 3 (Black to move). | `... w - a1` produced square -8, used to index `ep_mask`/`ep_codes`. |
| Bound the repetition list instead of relying on assert | When the 1024-entry list is full, keep the newest 512 entries; `repCount()`/`anyRep()` never read before the start. Very long `position` move lists log a warning. | Only an `assert` guarded it, and asserts are compiled out of release builds; ~1000 plies overwrote `repListHead`. |
| Add unit tests for FEN validation and long games | `testFENValidation`, `testLongGame` in `src/unit.cpp`. | Regression tests: all fail or crash under ASan on the old code. |
| Avoid signed overflow in GETBIT64/GETBIT32 | Unsigned arithmetic. | Undefined behavior flagged by UBSan (same machine code). |
| setSecondaryVars: skip a missing king | No shift by `InvalidSquare` (127). | Undefined behavior flagged by UBSan for FENs missing a king. |
| Add ARASAN_EMBEDDED build flag | See below. | Safe defaults for running inside an app. |
| readFEN: reject positions that can't occur in a game | Reject: the side not to move in check (incl. adjacent kings), more than one king per side, pawns on the 1st/8th rank. | Found by fuzzing: the engine could "capture" a king; in release builds `kingPos` became 127 and was used as an index. |
| readFEN: reject more men than a game allows | At most 8 pawns per side, and extra pieces only as many as missing pawns. | Found by fuzzing: `Material` stores each count in 4 bits, so 16 pawns overflowed into the knight count. |
| readFEN: require a real pawn behind the en passant square | The e.p. square needs the opponent's pawn on the double-move square, with the passed and origin squares empty. | Found by fuzzing: e.p. captured a knight as if it were a pawn, corrupting the bitboards. |
| Build without SYZYGY_TBS, and for iOS | Guard syzygy-only option code; no `libproc.h` outside macOS. | Building without tablebases didn't compile; `proc_pidpath` doesn't exist on iOS. |
| UCI position: reject moves that leave the king in check | `legalMove()` instead of `validMove()` in the `position` handler. | Found by fuzzing: same king-capture corruption, through a move list. |

`tests/fuzz/` has libFuzzer harnesses for `BoardIO::readFEN` and
`Notation::value` (UCI); see its README.

### ARASAN_EMBEDDED (`make EMBEDDED=1`, or `-DARASAN_EMBEDDED`)

- `initGlobals()` doesn't touch the process's stack `rlimit`.
  `delayedInit()` returns `false` instead of calling `exit()` when the
  network can't be loaded; UCI `go`/`bench` then report an error. (In all
  builds, a `setrlimit` failure now makes `initGlobals()` return `false`;
  its callers already exit on that.)
- No `arasan.rc` and no home-directory lookups. `store_games`, `ponder`,
  position learning, tablebases, the opening book and ECO lookup default to
  off.
- Commands and options that open file paths are refused: `test`,
  `loadgame`, `egtpath`, and the `BookPath`/`OwnBook`, `Game pathname`/`Store
  games`, `Learning file`/`Position learning`, `NNUE file` and
  `SyzygyPath`/`Use tablebases` options (UCI and xboard).

## Building

### Mac (development, tests)

```sh
git submodule update --init src/syzygy      # only needed with tablebases
cd src
make clean && make -j BUILD_TYPE=neon CC=clang            # engine
make clean && make -j BUILD_TYPE=neon CC=clang unit       # unit tests
ln -sf ../network/arasanv9-20261023.nnue ../bin/
../bin/unit                                 # 2 known testECO failures: they need the
                                            # book/chess-openings submodule
../bin/arasanx-64-neon bench
```

The Makefile doesn't track header dependencies, so `make clean` after
editing a header. Sanitizer build: add
`CXXFLAGS="-fsanitize=address,undefined -g" LDFLAGS="-fsanitize=address,undefined"`.

### iOS (device and simulator)

Compile these files from `src/` as C++17, without the Makefile:

```
globals.cpp board.cpp boardio.cpp material.cpp chess.cpp attacks.cpp
bitutil.cpp chessio.cpp epdrec.cpp bhash.cpp scoring.cpp see.cpp
movearr.cpp notation.cpp options.cpp bitprobe.cpp bookread.cpp bookwrit.cpp
search.cpp searchc.cpp learn.cpp movegen.cpp hash.cpp calctime.cpp eco.cpp
legal.cpp stats.cpp threadp.cpp threadc.cpp evaluate.cpp
nnue/features/ppfeature.cpp
```

(`bitbase.cpp` is `#include`d by `globals.cpp`; don't compile it separately.
Add `protocol.cpp input.cpp` only if you want the text UCI protocol.) Leave
out `arasanx.cpp` (main), `unit.cpp`, `nnuetest.cpp`, `tester.cpp`,
`bench.cpp`, `syzygy.cpp`, `src/syzygy/`, `src/util`, `gui/` and `tools/`.

Flags:

```
-std=c++17 -O3 -fno-rtti -DNDEBUG
-D_MAC -D_64BIT -DUSE_INTRINSICS -DSIMD -DNEON -DARASAN_EMBEDDED
-I src -I src/nnue -DNETWORK=arasanv9-20261023.nnue -DARASAN_VERSION=rooky
```

Do **not** define `SYZYGY_TBS`. Verified to compile for
`arm64-apple-ios18.0` and `arm64-apple-ios18.0-simulator`.

Bundle `network/arasanv9-20261023.nnue` (about 27 MB) as an app resource and
pass its absolute path at init. Skip `book/` (about 28 MB) for now.

**Thread stack:** search thread 0 runs on the thread that calls the search.
iOS gives secondary threads 512 KB, and Arasan wants 4 MB
(`globals::LINUX_STACK_SIZE`; extra search threads get it automatically).
Run searches on a thread created with `stackSize = 8 MB` (e.g. a dedicated
`Thread` in Swift), never on the main thread or a Swift concurrency
executor.

## Proposed C API

Arasan keeps its state in globals, so there is **one engine per process**.
The shim is a few hundred lines of C++ over `SearchController`; it doesn't
need the text protocol.

```c
// arasan_c.h
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ARASAN_OK = 0,
    ARASAN_ERR_NOT_INITIALIZED,
    ARASAN_ERR_NETWORK,        // .nnue missing or invalid
    ARASAN_ERR_BAD_FEN,
    ARASAN_ERR_ILLEGAL_MOVE,   // index in *bad_move_index
    ARASAN_ERR_NO_MOVES,       // checkmate or stalemate
    ARASAN_ERR_BUSY            // a search is already running
} arasan_status;

// Once, on the engine thread. hash_mb: 16..32 is plenty on a phone.
arasan_status arasan_init(const char *nnue_path, int hash_mb);

// Clears hash tables and history between games.
arasan_status arasan_new_game(void);

// fen: a position Rooky generated. moves: UCI strings ("e2e4", "e7e8q").
// Every move is checked with legalMove(); on failure nothing changes.
arasan_status arasan_set_position(const char *fen, const char *const *moves,
                                  size_t move_count, size_t *bad_move_index);

typedef struct {
    int movetime_ms;   // > 0: fixed time per move. Required for strength < 100.
    int depth;         // > 0: depth cap (0 = none)
    int strength;      // 0..100 (100 = full strength); see "Playing strength"
} arasan_limits;

typedef void (*arasan_bestmove_cb)(const char *uci_move, int score_cp, int depth,
                                   void *context);

// Blocks until the search ends (limit reached or arasan_stop()), then calls
// cb once on the same thread. Call on the engine thread (8 MB stack).
arasan_status arasan_search(const arasan_limits *limits, arasan_bestmove_cb cb,
                            void *context);

// Thread-safe, from any thread: ends the current search soon (cb still
// fires with the best move so far). No-op when idle.
void arasan_stop(void);

// Frees hash tables and threads. Call on the engine thread.
void arasan_shutdown(void);
```

How it maps onto Arasan:

- **init:** `BitUtils::init(); Board::init(); globals::initOptions();
  Attacks::init(); Search::init(); globals::initGlobals();` set
  `globals::options.search.nnueFile` (absolute), `hash_table_size`,
  `ncpus = 1`, then `if (!globals::delayedInit(false)) return
  ARASAN_ERR_NETWORK;` and create one `SearchController`.
- **set_position:** `BoardIO::readFEN()` into a scratch `Board`, then for
  each move `Notation::value(board, side, Notation::InputFormat::UCI, m)` +
  `legalMove()` + `doMove()`. Copy to the real board only if all succeed.
- **search:** set `globals::options.search.strength`, then
  `controller->findBestMove(board, srcType, time, 0, depth_or_MaxPly,
  false, false, stats, TalkLevel::Silent)` with `srcType = FixedTime` when
  `movetime_ms > 0`, else `FixedDepth`. Report the move with
  `Notation::image(board, move, Notation::OutputFormat::UCI, s)`.
- **stop:** register a `MonitorFunction` with
  `controller->registerMonitorFunction()` that returns
  `stopRequested.load()`; `arasan_stop()` sets that `std::atomic<bool>`.
  The search polls the monitor during its time checks, so nothing writes
  engine state from another thread.
- **shutdown:** delete the controller, `globals::cleanupGlobals()`.

## Playing strength (what Arasan actually supports)

Arasan has one strength knob, `globals::options.search.strength`, **0..100**
(`src/options.h`). The UCI options map onto it: `UCI_LimitStrength` plus
`UCI_Elo` (1000..3450) give `strength = 100 * (elo - 1000) / 2450`, so any
Elo of 1000 or below is strength 0. These are Arasan's own computer-rating
numbers, not Chess.com or Lichess ratings.

Below 100 it does three things (`src/search.cpp`):

1. **Caps the search depth:** `STRENGTH_DEPTH_LIMITS[strength * 2 / 5]`,
   from 1 ply at strength 0..17, 2–3 plies around 20, up to 20 plies at 99.
   It adds 1–2 plies in low-material endgames (strength > 10) so it can
   still mate.
2. **Sometimes plays a worse move** (`SearchController::suboptimal`): with a
   probability from 73% (strength 0) down to 5% (99) it picks from the top,
   middle or bottom of the root move list, within a tolerance from about 12
   pawns (0) down to 0.75 pawns (99). Below about 50 it can also pick moves
   worse than the tolerance, which means real blunders. It does this less
   when the best move is a capture, a promotion, a mate or out of check. It
   only works with `multipv = 1`.
3. **Takes its time:** on the last iteration it may sleep up to 2/3 of the
   time limit, so weak settings don't answer instantly.

**This only applies to searches with a time limit.** A pure fixed-depth
search ignores `strength`. Always give bots a `movetime_ms`.

Starting points for Rooky's bots, to tune by playing (the steps aren't
evenly spread, so test around these):

| Bot | strength | movetime_ms | Notes |
| --- | --- | --- | --- |
| True beginner | 0–5 | 300–500 | 1 ply, frequent big blunders |
| Casual | 15–20 | 500 | 1–2 plies, regular mistakes |
| Club | 35–45 | 800 | 6–9 plies, occasional mistakes |
| Strong club | 60–70 | 1000 | 9–10 plies, rare mistakes |

Arasan has no style settings (aggression, trading), so personality has to
come from the app: think-time ranges, chat lines, opening choices once a
book is added.

## Rules for the app side

1. Send the engine **only FENs and UCI moves that Rooky's own Swift code
   generated and validated.** Never pass raw user text, pasted text or
   imported game text (Chess.com/Lichess PGNs go through Rooky's own
   parser first, then Rooky re-encodes the result).
2. **Strip `\r` and `\n`** from anything sent, so one string can never
   become two commands.
3. **Check every move the engine returns** against Rooky's own legal-move
   list before playing it. If it isn't legal, log it and recover (e.g. play
   Rooky's own move or treat it as a resignation).
4. **End games by the 75-move rule and fivefold repetition** (automatic
   draws under FIDE rules), so a game can never grow long enough to stress
   the engine's history limits.
5. Prefer sending the current FEN plus only the moves since the last
   capture or pawn move: shorter input, same repetition detection.
6. Use one search thread, a 16–32 MB hash, pondering off. Call
   `arasan_stop()` when the app goes to the background or the player leaves
   the game.
