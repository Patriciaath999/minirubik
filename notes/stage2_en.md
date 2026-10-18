## Stage 2: Representation and algorithm for the target

Every choice below is justified against the two stage-1 measurements on this machine (Ripes v2.2.6-106-g5b8a616, Windows x86-64) and against the budget: ≤ 128 KiB static data, ≤ 5 × 10⁷ retired instructions on RV32_ISS for any distance-11 state.

| Stage-1 figure | Value | Consequence for the design |
|---|---:|---|
| Host bytes per guest byte (sparse memory) | 80.27 | Every guest byte costs ≈ 80 B of host RAM; the baseline's 18,405,414 B would be ≈ 1.48 GB |
| RV32_ISS, memory loop | 21.6 M instr/s | 10⁹ instructions ≈ 46 s |
| RV32_5S, memory loop | 227 k instr/s | 10⁹ instructions ≈ 73.5 min |
| RV32_6S_DUAL, memory loop | 75 k instr/s | 10⁹ instructions ≈ 3.7 h |

Memory is not fatal on this host (1.5 GB of 32 GB), but the 128 KiB rule forbids the full table anyway. Simulation rate is the binding constraint: the design has to cut the instruction count by at least an order of magnitude, and the pipelined models have to be able to step through one query.

### 2.1 Representation

**Cubie model only at the boundary.** The 14-digit input is parsed into `p[7]`, `o[7]` exactly as in `solver.c` (corner 0 fixed, so U/F/L never move and only R, B, D are generators). This representation is used once, to rank the start state; it never appears in the search loop.

**Search state = two factored coordinates.** report.md §4 establishes that a face turn permutes the corners and twists them independently of where they are, so the state space factors as

- permutation of 7 cubies → Lehmer rank, 0 … 5039 (7! = 5040)
- orientation of 6 cubies → base-3 rank, 0 … 728 (3⁶ = 729; the seventh twist is implied by the sum ≡ 0 mod 3)

and the full space is the product, 5040 × 729 = 3,674,160. Instead of one 22-bit rank the state is kept as the pair (p, o), each in its own register. This is the whole point of the representation: a move on the pair is two independent table lookups, and neither table is anywhere near the size of the product.

**Transition tables (host-built, `.rodata`).**

| Table | Entries | Width | Bytes |
|---|---:|---|---:|
| `perm_move[3][5040]` | 15,120 | u16 | 30,240 |
| `orient_move[3][729]` | 2,187 | u16 | 4,374 |

Only the three quarter turns R, B, D are stored. `X2` and `X'` are one and two further applications of the same quarter turn, so the 9-move table the baseline carries as `[9][…]` shrinks by 3×, and during search each face costs three lookups (X → X2 → X') instead of six (1 + 2 + 3). Index width is u16 because 5039 < 65536; u32 would double the table for nothing.

**Heuristic tables = pattern databases over the same two projections.**

| Table | Entries | Width | Max value | Bytes |
|---|---:|---|---:|---:|
| `perm_dist[5040]` | 5,040 | u8 | 7 | 5,040 |
| `orient_dist[729]` | 729 | u8 | 6 | 729 |

Each is an exact BFS distance on the abstraction, built on the host from `source`/`twist` (`solver.c --emit-tables`) and linked in as read-only data. The abstractions are the two projections the group already factors into, so the fibers are equal-sized (729 and 5040 states collapse onto each entry) and the tables are smaller than the state space by exactly those factors.

**Why these two and not something bigger.** The index of a PDB entry is the same number as the index of a transition-table entry, so the heuristic costs one byte load per child with no extra ranking. A finer abstraction would need its own rank and its own move table; the candidates and their cost:

| Abstraction | Entries | Bytes (u8) | Fits 128 KiB? |
|---|---:|---:|---|
| perm × full orient (= complete distance table) | 3,674,160 | 3,674,160 (1,794 KiB packed to 4 bits) | No, and forbidden by rule |
| perm × 4 twists (3⁴) | 408,240 | 408,240 | No |
| perm × 3 twists (3³) | 136,080 | 136,080 unpacked / 68,040 packed | Packed only, ≈ 7 instr per nibble load in the inner loop |
| perm alone + orient alone (chosen) | 5,040 + 729 | 5,769 | Yes, with 94 KiB to spare |

The packed 3-twist table was not built: the search below already runs 3.9× under the instruction budget, and the ≈ 5 extra instructions on every heuristic lookup would be paid on every one of the ~6.4 × 10⁵ nodes of the hardest query before any pruning gain is seen. It stays listed as the next thing to try if the budget were tightened.

**Totals.** 40,383 B of tables + 384 B search stack + strings and parse buffers = **40,811 B static data, 31% of 128 KiB**. Peak guest memory touched during a query is the same 40 KiB (the tables are read, 12 frames are written), so at 80.27 host B per guest B the whole program costs the host ≈ 3.3 MB instead of ≈ 1.5 GB. The 14,696,640-byte BFS queue and the 3,674,160-byte move table of the baseline are gone, not shrunk.

### 2.2 Algorithm: IDA* with max of two PDBs

**Why not the baseline BFS.** It needs the queue (79.8% of the baseline peak) and the per-state table, both forbidden, and ≈ 10⁹ instructions: 46 s on ISS, 73 min on RV32_5S, 3.7 h on RV32_6S_DUAL by the stage-1 rates. Enumerating all 3,674,160 states at a few hundred instructions each is ≈ 10⁹ regardless of how it is done, 14–20× over the 5 × 10⁷ budget, so any design that visits the whole space is out.

**Why not A\* or bidirectional search.** Both need an open list or a visited set that grows with the search: a heap or a hash set, and the assignment allows no heap and no recursion. Everything in the search has to be sized at assembly time.

**IDA\*.** Depth-first search with cost bound `bound`, restarted with `bound + 1` until a solution is found; memory is one frame per depth, 12 × 32 B = 384 B, allocated statically. The search is pruned at a node of depth g + 1 when `g + 1 + h > bound`.

- `h(p, o) = max(perm_dist[p], orient_dist[o])`
- `bound` starts at `h(p₀, o₀)`, never at 0; it is at most 11, so the loop terminates (the diameter is 11, confirmed by H3 on the host).
- Same-face pruning: a move of face X is never followed by another move of face X (it would cancel or compose into one move), so the branching factor is 9 at the root and 6 below. This costs one pointer compare per face and removes the 1/3 of the tree that cannot hold a shortest solution.
- Move order within a face: X, X2, X' by repeated quarter turn on the child already in hand.

**Optimality.** IDA* returns a shortest path whenever `h` is admissible, i.e. never exceeds the true distance:

- A projection of the cube group onto the permutation (or the orientation) coordinate maps every generator to a generator of the abstract graph, so any path of length k in the full graph projects to a walk of length ≤ k in the abstraction. The exact abstract BFS distance is therefore ≤ the real distance for every state.
- The maximum of two admissible heuristics is admissible.
- The tables are exact BFS distances, so they are also consistent; no pruning of an optimal path can occur.

This is checked, not assumed: gate H1 compares `max(perm_dist, orient_dist)` with the exact distance for all 3,674,160 states (0 violations), H2 checks both tables are fully populated with solved entry 0 and maxima 7 and 6, and H3 runs the search on every state and compares the returned length to the exact distance (0 wrong). A deliberately corrupted `perm_dist[9]` (1 → 8) makes H1 report 92 violations, which shows the gate can fail.

**Cost against the budget.** From the host-side node count (gates.c) and the retired count of the RV32I build:

| Query | Nodes expanded | Retired instructions (RV32_ISS) | Budget share |
|---|---:|---:|---:|
| `21345671111111` (reference, d = 11) | — | 4,664,552 | 9.3% |
| `54721631111111` (hardest of 2,644 d-11 states) | 639,792 | 12,753,391 | 25.5% |
| easiest d-11 state | — | 2,817,005 | 5.6% |
| mean over all 3,674,160 states | — | 324,059 | 0.6% |
| solved cube | 0 | 327 | — |

The hardest distance-11 query costs ≈ 20 retired instructions per expanded node and 3.9× less than the 5 × 10⁷ ceiling. Worst-to-best ratio among distance-11 states is 4.5×, which is why every one of the 2,644 was run (d11.txt) rather than one sample. On this machine the hardest query takes ≈ 0.6 s on RV32_ISS, ≈ 56 s on RV32_5S and ≈ 2.8 min on RV32_6S_DUAL, so stepping through a query on the visual pipeline models is practical; the baseline's 73 min / 3.7 h is not.

**Where the time goes, and why the final bound dominates.** With branching factor 6 the tree at bound b is ≈ 6× the tree at b − 1, so the last iteration costs more than all earlier ones combined. report.md's distance distribution (51.4% of states at distance 9, 92.0% between 8 and 10) means a typical query runs out to bound 9 or 10; a distance-11 query runs the full bound-11 iteration, which is why that class is the pass condition.

### 2.3 Arithmetic the target cannot do

RV32I has no `mul`, `div` or `rem`, and the compiler helpers `__mulsi3`/`__divsi3` are forbidden. The representation was chosen so that nothing in the search loop needs them:

| Operation in the baseline | Where it went |
|---|---|
| `p * (7 − i)` in `perm_rank` | Runs once per query at parse time; done by repeated addition (≤ 7 adds) |
| `o * 3` in `orient_rank` | `(o << 1) + o`, once per query |
| `p * 729`, `/ 729`, `% 729` (combined rank) | Gone: p and o are never combined into one number |
| `move / 3`, `move % 3` (face, turn) | Gone: the face is kept as a pointer into its table block (stride 10,080 B for `perm_move`, 1,458 B for `orient_move`, advanced by `add`), the turn as a 0–2 counter |
| `(o + twist) % 3` in `quarter_turn` | Gone from the target: orientation arithmetic happens on the host when `orient_move` is built |
| `sum % 3` in `valid` | Not needed in the search; input validity is a host-side concern (gates) |

Table strides are plain additions on a pointer, and the only index arithmetic in the inner loop is `rank << 1` for the u16 tables and `rank` itself for the u8 tables, so no stride reintroduces a multiply.

### 2.4 Summary of the choices

| Decision | Choice | Driven by |
|---|---|---|
| State | (perm rank, orient rank), two registers | §4 factorisation; keeps every table ≤ 30 KiB |
| Move tables | 3 quarter-turn tables, u16, 34,614 B | 1/3 of the 9-move table; X2/X' by repetition |
| Heuristic | max of two exact PDBs, u8, 5,769 B | same indices as the move tables → one load per child; admissible by projection; verified by H1 |
| Search | IDA*, static 384-B frame stack, same-face pruning | no heap, no recursion; worst case 1.28 × 10⁷ ≪ 5 × 10⁷ |
| Precomputation | both PDBs and move tables built on the host, linked as `.rodata` | allowed by the rules; 40,811 B ≪ 128 KiB |
| Arithmetic | no mul/div/mod anywhere on the target | RV32I only; ranks computed once, faces as pointers |
