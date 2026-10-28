# Optimization thoughts (no compiler code yet)

## 1. What is scored
- Correctness first (8%), then the **sum of executed BF steps** decides rank (2%).
- Step = every executed `> < + - . , [ ]`. A loop `[...]` costs `1 + n*(body+1)`, because `]` jumps *after* `[`.
- The runner prints the newline itself, so `print(x)` is one `.` plus pointer movement.

## 2. Where the steps actually go
Everything expensive is a **loop over a runtime value** (about 128 iterations on average).
- Move or copy a value: `[->+<]` is `5v+1`. Copy to two cells is `8v+1`.
- Constant add: `min(k, 256-k)` steps. It is free of loops, and `-` is used for k>128.
- Multiply by a constant k: `(k'+4)*v`, with `k' = min(k, 256-k)`.
- Multiply two runtime values (naive nested loop): about **250k steps on average**, up to about 1M.
  I measured this with the real runner.
- The random benchmark cases contain many `var*var`, `c*c` and 3-factor products.
  **Runtime×runtime multiplication dominates the total score.** Everything else is noise until that is fixed.

## 3. Ideas, ranked

| Idea | Verdict |
|---|---|
| Parse and validate whole file first; exit 43 on any error | Required |
| Dead-code elimination (only `print` is observable; `input()` must still execute `,`) | Big, cheap |
| Constant folding and value numbering (CSE) over canonical polynomials mod 256 | Big, cheap. Use the polynomial form only as an equality key, not as the emission plan. Expanding `(b+c)*(a+d)` costs 4 products instead of 1. Cap expansion size. |
| Copy propagation: `b=a` is an alias, no code | Free win |
| Track each cell as `atom + known offset`. `a=a+1` costs 1 step, and `print(a+k)` costs `+k . -k` | Big, cheap |
| Track known cell contents (e.g. 0 after `[-]`), so no redundant clearing. `,` overwrites, so no clearing before input | Cheap |
| Consume (move) a value at its last use. Copy only when it is still live. A copy loop can also create the variable's new home, so there is no restore pass | Medium |
| Locality: put temps and destinations next to the source (pointer moves cost 1 each) | Medium |
| Multi-destination passes (one loop over `a` feeds several targets) | Medium |
| Sign trick, `min(v, 256-v)` | **Rejected.** Reading a raw cell costs O(v) before any adaptivity. It only helps near 255 and gives about 33k per product on average. |
| Ping-pong twin cells to avoid the restore pass | Rejected for now. The alternation needs a pointer-resync trick per iteration, and it only saves about 25%. |
| **Nibble split (radix 16) multiplication** | **Best idea found.** See section 4. |
| Log/exp or quarter-square tables | Rejected. Table init is about 30k steps and each lookup is O(index). |
| Constants: factor chains, or nibble method for large k | Medium. Choose by a cost model, because direct `(k'+4)*v` is up to 17k. |

## 4. Nibble multiplication (the key idea)
Write `u = ul + 16*uh` and `v = vl + 16*vh`. Mod 256 the `uh*vh` term vanishes, so

`u*v = ul*vl + 16*(ul*vh + uh*vl)`

Every inner loop now runs over a value of at most 15. Squares need less:
`a*a = ul^2 + 32*(ul*uh)`.

Extraction is a **ladder**: 16 nested guarded steps `[-<+>[-<+>[ ... ]]]`.
A skipped guard falls through all the closers.
One marker cell (`>[<]`) resyncs the pointer once per 16 units, not once per unit.

Measured with the official `bf.cpp`:

| Item | Steps |
|---|---|
| Naive `u*v` (copy + restore), random average | ~250,000 |
| Naive `255*255` | ~978,000 |
| Nibble multiplication, simple extraction (Y=16x pass), random average | ~14,400 |
| Ladder extraction alone, all 256 inputs correct, average | ~963 (max 1,908) |

Estimate for ladder extraction plus 3 small products: **about 5–7k per product**, roughly 40x better than naive.
This estimate is **not yet measured**: the combined run was cancelled.

## 5. Same machinery helps constants
Large `k` such as `x*127` costs about 17k directly. The nibble method costs about 1.2k (extraction, shared across uses of `x`) plus about 1.2k.
So: a variable may hold a raw cell **and** a lazily created (lo, hi) nibble pair, and the cheapest route is chosen by a cost model.

## 6. Risks and open questions
- Nibble products add code complexity. Plan: first a correct baseline with naive products, then swap in nibble products and compare with `make cases`.
- Hidden tests stress 10,000 variables and 500 nested parentheses. Recursion depth is fine, but the polynomial hashing needs size caps, and cell reuse via liveness keeps the tape small.
- Should squares and constants go through the nibble path only when the cost model says it wins (e.g. skip it for k' ≤ ~12)?

## 7. Proposed order
1. Parser, validation, exit 43, naive but correct BF.
2. DCE, folding, aliasing, offset tracking, last-use moves.
3. Value numbering plus the factoring heuristic.
4. Nibble multiplication, then squares, then large-constant multiplication.
5. Measure on `make cases`, and keep a cost-model table for choosing gadgets.

Scratch experiments live outside the repo in the Antigravity brain `scratch/` folder (`proto.py`, `ladder.py`, `s2.py`).
