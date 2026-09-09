# HolonNPU Progress

Last updated: 2026-09-09.

## Current Work

The ADR-0066/0067 canonical execution and autonomous timing bring-up checkpoint
is **verified**. The full simulation foundation remains in progress: detailed
performance modeling, wider system verification and ISA/public-contract convergence
are not complete. Git history and CHANGELOG retain prior accelerator evidence;
that evidence does not substitute for the autonomous tests below.

- The canonical C++26 machine executes RV32IM/Zicsr M-mode instructions and
  redesigned 64-bit Holon vector/predicate/matrix/DMA instructions. No old
  interpreter, descriptor device or compatibility execution mode remains here.
- The mixed-width runtime builder, semantic/runtime tests and compiled ELF probes
  use that machine. The environment, not the core, owns external memory.
- Transformer: one 5,452-byte shape-specialized guest program implements embedding,
  causal attention/softmax, residuals, affine LayerNorm, ReLU FFN and logits.
  Ten fixtures across three vector capacities produce 30 complete executions,
  each retiring 993 instructions. Sixteen stages are checked independently.
- gem5: the Host/DmaDevice adapter, bare-metal Host and Linux driver paths have
  been removed. The replacement ClockedObject boots autonomously, shares the
  core, and uses a timing RequestPort. Full build, provenance audit and four
  Transformer timing scenarios pass. No calibrated hardware prediction is claimed.
- Semantic footprints carry bounded lane/shape/traffic information, not cycle
  parameters. Invalid VL/matrix shapes cannot create unbounded modeled work;
  precise architectural faults still occur at completion.

## Verified In This Cutover

| Gate | Latest evidence |
| ---- | --------------- |
| Debug build and full debug CTest | Passed; 32/32 tests |
| Lint CTest | Passed; 11/11 tests |
| Execution/Transformer after footprint bounds fix | Passed; 2/2 tests |
| ABI and ISA generated-source checks | Passed; 3 ABI outputs and 6 ISA outputs |
| Macro policy and whitespace | Passed |
| Optimized regression build and full CTest | Passed; 43/43 tests |
| ASan/UBSan semantic, hart, execution and Transformer tests | Passed; 4/4; leak detection disabled under sandbox ptrace |
| Canonical compiled C23/C++26 ELF probes | Passed through gem5 preset's scalar toolchain gate |
| Autonomous gem5 preset | Passed; 6/6 tests, including four Transformer timing scenarios |
| Coverage build and full coverage CTest | Passed; 45/45 tests |
| RTL coverage checker | Passed; 12 raw files, 137 functional points, 56 cover properties |
| gem5 provenance audit | Passed; 27,587 C++ compilation entries use C++26; strict numeric flags checked |

Selected numerical evidence: Transformer maximum absolute error `5.44154e-5`
against an independent double reference with predetermined absolute/relative
tolerances `2e-4`. Near-constant LayerNorm and large attention scores are included.
The exported seed-17 fixture has maximum error `2.12183e-7`. These are functional
results, not trained-model accuracy or performance measurements.

Runtime/semantic tests include 2,077 constant-materialization programs, all
4,096 ADDI immediates, 828 integer programs across six types/three capacities,
mask/tail and aliasing, numeric edges, matrix views, failed-load atomicity,
stable DMA store payload, precise fault PC, and stale/duplicate token rejection.
Coverage of these cases must not be confused with exhaustive ISA verification.

Latest commands: `cmake --build --preset debug --parallel 2`, the corresponding
`regression` and `coverage` builds, `ctest --preset debug --output-on-failure`,
the corresponding `lint`, `regression` and `coverage` tests, and
`python3 tools/check_coverage.py --build-dir build/coverage` all passed.
The structural RTL results are line 65.1%, branch 61.0%, toggle 31.4% and expr
54.6%, above their unchanged baselines. They do not measure the new gem5 path.

Scalar and DMA completion tests now validate an optional failed physical byte
address: out-of-request addresses are rejected without consuming the token;
accepted faults preserve precise PC/retirement and report the failed address
in MTVAL. Failed DMA loads leave local memory unchanged. gem5 supplies the
failed packet address, but error injection through that adapter remains pending.
The sanitizer run used `ASAN_OPTIONS=detect_leaks=0` and
`UBSAN_OPTIONS=halt_on_error=1` with the existing `build/semantic-sanitize` tree;
this verifies address/undefined behavior checks, not leak freedom.

## Autonomous gem5 Evidence

Built at 16 jobs from official upstream `stable` commit
`f5c5a6e390f55dd5984977815bf9d0bd05da6945`, using GCC 16.2 and the tracked C++26
overlay. `tools/build_gem5.py` completed build, incremental verification,
compilation-database generation and metadata output. The gem5 source set excludes
the synchronous direct runner and test program builder, enforced by the audit.
`ctest --preset gem5
--output-on-failure` passed. Evidence is under `build/gem5/simulation-metadata.json`
and `build/gem5/gem5-tests/autonomous/` (config, stats, program/input/output,
reference and sensitivity report).

At 1 GHz with the documented noncoherent crossbar:

| Scenario | Cycles | Timing retries |
| -------- | ------ | -------------- |
| 20 ns memory, 16 lanes, 4 GiB/s | 14,162 | 0 |
| 100 ns memory | 34,642 | 0 |
| One vector lane | 15,473 | 0 |
| 64 MiB/s memory bandwidth | 247,758 | 254 |

All four runs preserve complete memory bytes, final PC 9,548, 993 retirements,
zero traps, 552 matrix MACs and 16,384 external bytes in 256 packets. Exclusive
tick ledgers reconcile exactly; all four stderr files are empty. These results
validate a blocking timing model and sensitivity, not calibrated RTL performance.

## ISA And RTL Boundary

The repository has an unresolved authority split: the schema's top-level old
instruction table still generates the released RTL package/public ISA header,
while `semantic_frontend`/`semantic_npu` generate the redesigned executor's
metadata. `docs/ISA.md` still describes the old encoding. This is known migration
debt, not a supported dual-ISA design or a completed ISA cutover.

Released RTL remains the ABI 3.0/ISA 1.0 accelerator baseline. Test-only
`sim/rtl_program.*` builds old RTL inputs and uses independent mathematical
scoreboards; it contains no interpreter and is not linked into the new runtime.
No new RTL capability or simulator-first approval is implied by those tests.
The earlier generation remains recoverable from the existing release tags.

## Remaining Acceptance

1. Converge the ISA schema, public exports and main documentation on the new
   contract without exposing old encodings as current ISA or bypassing RTL review.
2. Expand packet-fault, boundary, external-fetch, trap/WFI/interrupt and checkpoint
   integration evidence beyond the passing Transformer/retry scenarios.
3. Complete detailed pipeline/resource modeling, timing calibration and a broader
   workload corpus. Establish required coverage events for the new path.
4. Keep all applicable gates passing and commit each verified feature promptly.
   Do not push without instruction.

## Scope And Limits

- Current autonomous timing is a blocking one-operation/one-memory-packet model;
  detailed pipelines, contention and calibrated performance are not complete.
- The Transformer is fixed-shape assembled code, not a dynamic graph compiler.
- gem5 WFI wakeup/checkpoint integration is not implemented; no Host test result
  substitutes for it. Structural and functional coverage must be re-established
  for the new path.
- No BF16, FP8, MMU/IOMMU, OS, multiple contexts or new RTL implementation.
- Formal proof, synthesis/PPA and physical implementation are not completed gates.
- Upstream gem5 warns that GCC 16.2 is outside its supported range; optional HDF5
  C++ support is unavailable. Its sources also emit deprecated capture/enum,
  unused-variable and statistics-vector array-bounds diagnostics with this
  compiler. These are retained, not suppressed; they are not Holon test failures
  or authorization to reduce C++26.
