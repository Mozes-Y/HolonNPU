# HolonNPU Architecture

## Current Baseline

HolonNPU 3.0 is an architecture-research platform. The implemented baseline is
an autonomous functional processor, not a synthesized chip or a calibrated
performance model. ISA and numerical behavior are defined in [ISA](ISA.md).

Execution: program builder or ELF image -> validated boot -> shared
`program_machine`/RV32 hart -> typed pending operation -> environment completion
-> architectural retirement or trap. Guest instructions perform all tensor
computation; the environment supplies system-memory service, not operators.

## Source Ownership

| Component | Responsibility |
| --------- | -------------- |
| `holon_npu_instruction.*` and generated metadata | Mixed-width decode, typed operands, encoding/disassembly |
| `holon_npu_scalar.*` | Pure RV32 evaluation into effects |
| `holon_npu_hart.*` | Registers, CSRs, traps, interrupts and precise scalar commit |
| `holon_npu_memory.*`, `holon_npu_elf.*` | Checked physical regions and transactional ELF validation/loading |
| `holon_npu_semantic.*`, `holon_npu_npu.cpp` | Canonical machine, vector/predicate/tile state and arithmetic |
| `holon_npu_execution.*` | Direct request servicing and resumable functional run budgets |
| `holon_npu_runtime.*` | Program construction, not another interpreter or graph compiler |

The core owns local program memory, scratchpad and architectural registers.
The caller owns external memory. Strong address/token types separate domains.
The machine exposes one pending operation; invalid/stale completions are API
errors, not fabricated architectural traps. Successful completion commits the
instruction; a fault preserves precise PC and retirement.

Vector state is VLA with explicit length/type/predicate operands. Matrix views
and logical tiles are separate; matrix output currently reaches vector code
through scratchpad. Logical capacity does not define a physical PE topology.
Operation footprints contain bounded counts, not performance predictions.

## Research Boundary

The independent timing model will reuse semantic effects and own resource
scheduling, contention and modeled time. It is not implemented by the current
serial direct runner. [Simulation](SIMULATION.md) defines determinism, fidelity
and evaluation rules; [research](research/README.md) contains candidates.

Block DAGs, soft placement, shared tile storage, scopes and block retirement
remain independent hypotheses. Preserve baseline semantics when comparing
microarchitectures. An ISA or commit/visibility change must be explicit, not
hidden in a scheduler option.

## Historical Implementations

RTL and gem5 adapters were retired in the 3.0 transition. Their complete
checkpoint is recorded in Roadmap. They do not constrain current public exports
or prove the new model correct. There is no in-tree alternate product path.

RTL development is paused. Resuming it requires a reviewed semantic/performance,
software-cost and verification case; no new hardware capability is implied by
a research model parameter.
