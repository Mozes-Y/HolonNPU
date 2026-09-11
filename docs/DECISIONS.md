# HolonNPU Decisions

Only currently applicable architectural and engineering decisions belong here.
Git and Changelog retain superseded implementations and incremental milestones.

## ADR-0068: Research Mainline And RTL Pause

**Status:** Accepted for the HolonNPU 3.0 transition.

**Decision:** Retire the old accelerator RTL, descriptor/MMIO ABI/driver, test
harness and gem5 integration from the mainline. Preserve one current C++26
semantic implementation, independent test oracles and portable workload inputs.
Historical recovery uses `9e70a99ef0f694cbd503f259aadf99317c76344b`, not in-tree
legacy copies. No release tag is moved. Project 3.0 is not new ISA/ABI numbering.

**Why:** Keeping an older product beside a redesigned interpreter creates
competing schema, documentation and coverage authorities. Disabling targets
alone does not remove that split. Reimplementing RTL now would freeze an
unvalidated architecture.

**Research contract:** The user's explicit tile-dataflow architecture is a
core candidate to validate in full and by ablation, not a predetermined winner.
Use workload-driven, resource-constrained comparisons with counterexamples.
New ideas enter as explicit hypotheses. The independent C++26 performance
model is execution-driven, mixed-fidelity and deterministically parallel.
No new timing framework is claimed by source cleanup.

**Supersedes:** Mandatory gem5, old accelerator ownership/interface policies
and RTL-centric release gates. Those historical decisions remain in Git.
RTL can resume only after a later ADR approves measured semantic, performance,
software and implementation-cost evidence.

## Continuing ISA Decisions

ADR-0059/0060/0062/0063/0065 selected the current baseline: RV32IM/Zicsr,
ILP32, M-mode and a unified 32-bit physical space; no RVC; 32-bit scalar and
64-bit Holon instructions; explicit VLA/predication and logical matrix tiles.
The motivation is standard scalar toolchain reuse without constraining Holon
operand formats to RVV encoding. Exact current behavior lives only in ISA.

Precise instruction retirement remains the baseline. Explicit async tasks,
block visibility/commit and different numerical orders are semantic research
changes, not silently interchangeable scheduler policies.

## Continuing Engineering Decisions

- One schema owns current executable instruction metadata. Generation is
  reproducible and checked; handwritten contracts describe behavior, not copied
  encoding tables. Historical ABI/ISA fields are not retained as aliases.
- Use C23/C++26, typed interfaces and stdlib facilities. Project behavior macros
  are prohibited; standard interoperability/library facilities are permitted.
- Target-centric CMake declares sources/headers on targets. CTest owns execution.
  Presets pin only essential build configurations, never subsystem shortcuts.
- Commit each tested feature with its current documentation. Do not create an
  ADR for routine implementation progress; only decisions with lasting tradeoffs.
- Numerical tests use independent mathematical references. Coverage reports
  describe the implementation they actually instrument, not historical counts.

## ADR-0069: Native C++ Verification Evidence

**Status:** Accepted and implemented for the 3.0 mainline.

**Decision:** Coverage instruments the current C++ targets with GCC/gcov, not
testbench feature macros or a second logging runtime. Keep one coverage preset,
using an unoptimized instrumented tree for source attribution. Debug and
optimized regression remain uninstrumented. Record compiler identity.

**Gate:** Start from clean counters, require exact current translation-unit
data from the compilation database, check source/configuration provenance,
report owned source line/branch/function coverage, and enforce a measured
line/function baseline. Branches are reported, not equated to ISA functional
coverage. Functional correctness remains the actual test scoreboards; no old
RTL coverage point is credited. Baselines cannot be lowered by the update tool.

**Alternatives:** Keeping Verilator data would measure retired code. Per-test
logging/backend wrappers add machinery unrelated to source coverage. Native
compiler instrumentation preserves all C++ behavior without own feature macros.
The first coverage backend supports GCC; other compilers may build ordinary
configurations, but coverage cannot silently omit unsupported instrumentation.
