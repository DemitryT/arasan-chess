# Fuzz tests

libFuzzer harnesses (with AddressSanitizer and UndefinedBehaviorSanitizer)
for the code that parses untrusted input:

- `fuzz_fen`: `BoardIO::readFEN`. Every accepted position must have a
  consistent hash, and every legal move must make and unmake cleanly.
- `fuzz_uci_move`: `Notation::value` with UCI input. Input is an optional FEN
  line, then whitespace-separated moves, applied like the UCI `position`
  command does (parse, `legalMove`, `doMove`).

Asserts stay enabled, so an assert firing counts as a crash.

```sh
tests/fuzz/build.sh     # needs a clang with libFuzzer; on macOS: brew install llvm
mkdir -p tests/fuzz/out/work-fen tests/fuzz/out/work-uci
tests/fuzz/out/fuzz_fen -max_total_time=300 -max_len=256 \
    tests/fuzz/out/work-fen tests/fuzz/corpus/fen
tests/fuzz/out/fuzz_uci_move -max_total_time=300 -max_len=4096 \
    tests/fuzz/out/work-uci tests/fuzz/corpus/uci_move
```

New inputs go into the first directory (`out/work-*`, ignored by git); the
seed corpus is read only. Crash inputs are written to the current directory
(or `-artifact_prefix`) and can be replayed by passing the file as the only
argument.
