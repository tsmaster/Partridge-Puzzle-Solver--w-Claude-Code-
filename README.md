# Partridge Puzzle Solver

A C++ solver for the "Partridge Puzzle": tile a 45x45 square exactly with
one 1x1 square, two 2x2 squares, three 3x3 squares, ... up to nine 9x9
squares (45 tiles total, areas summing exactly to 45x45 = 2025).

This repository holds the actual search engine, `rangesolver.cpp` - a
backtracking solver (largest remaining tile first, into the first free
cell in raster-scan order) that can search the *entire* puzzle, or be
bounded to a specific slice of the search tree via `--start`/`--end`, so
independent processes (or machines) can work disjoint ranges in parallel.

The companion repository, [Partridge-Solver-Tools](https://github.com/tsmaster/Partridge-Solver-Tools),
is the Python coordination layer that actually runs this solver as a
distributed, multi-worker, multi-machine search - this repo is the engine,
that one is everything that drives it.

## Results

The full search is complete: **1,730,280 unique solutions** (216,285 if
rotations and reflections of the same tiling are counted once), an exact
match to the figure Matt Parker cites in ["The impossible puzzle with over
a million solutions!"](https://youtu.be/eqyuQZHfNPQ). See
[`RESULTS.txt`](RESULTS.txt) for the full write-up, including:

- An independent confirmation of the n=8 sub-case (18,656 solutions)
- A complete resolution of the video's "L"/"I"/"other" solution-category
  breakdown - including a swapped pair of labels and a broader definition
  of "I" than the video states
- A couple of unexplained structural findings in the complete solution set
  (an "interior" line pattern that only ever occurs at one position, up to
  rotation/reflection, with its own smaller recursive version of the same
  phenomenon)

[`TODO.txt`](TODO.txt) is the full, running development log - every
feature, measurement, bug, and dead end, roughly in the order it happened.
It's long, and more a lab notebook than user-facing documentation, but
it's the real story of how this was built (including dead ends -
optimizations that were tried, measured, and reverted when they made
things slower).

## Building

```
make
```

Builds with `-O3 -march=native`. The Makefile's own comment is the
important caveat: **build on the machine that will run the binary** -
`-march=native` tunes for whatever CPU does the compiling, so a binary
built on one machine isn't safe to copy and run on another.

## Usage

```
./rangesolver [--start=DIGITS] [--end=DIGITS] [--log=FILE] [--status=MODE]
./rangesolver --enumerate-depth=N [--start=DIGITS] [--end=DIGITS] [--out=FILE]
```

`DIGITS` is a string of 1-45 characters, each `1`-`9`, giving the tile
size placed at each successive raster-scan position - the same format a
solution's own hash takes. A string shorter than 45 characters names a
whole subtree of the search rather than one specific solution.

- `--start`/`--end` bound the search to a slice of the tree (inclusive on
  both ends) - omit either to leave that side unbounded. This is what
  lets the search be split across many workers/machines: see the tools
  repo's `worker.py` and `splitter.py`.
- `--enumerate-depth=N` switches to listing every valid N-digit prefix
  reachable within `[--start, --end]` instead of solving - each line is a
  ready-made `--start=P --end=P` pair, used to partition the search into
  initial units of work.
- `--log` overrides the solution output file (default: a timestamped
  `Solutions/soln_log_range_<timestamp>.txt`).
- `--status=inplace|scroll` controls whether the periodic progress line
  overwrites itself (nice for an interactive terminal) or scrolls (better
  when another process is parsing the output, or it's redirected to a
  file).

## License

MIT - see [`LICENSE`](LICENSE).
