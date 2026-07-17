# HolonNPU Interface Contract

This document defines the behavioral contract for the current programmable NPU
ABI. Exact values and layouts are generated in
`docs/INTERFACE_REFERENCE.md` from `spec/holon_npu_abi.json`.

## Ownership

- ABI version: 3.0 (`0x00030000`).
- ISA version: derived from `spec/holon_npu_isa.json`.
- Host control: 32-bit AXI-Lite.
- System memory: 128-bit AXI4 master.
- Public C ABI: generated C23/C++-compatible constants and structures.
- One active program is supported.

The ABI schema owns registers, descriptor and completion layouts, lifecycle
bits, flags, IRQs, faults, capabilities, and numeric values. The ISA schema owns
instruction and operation-class numbering. Handwritten code must not duplicate
those values.

## Submission

Software allocates and initializes:

1. a Holon program image;
2. an optional argument block;
3. optional input/output tensors;
4. an optional 32-byte completion record;
5. a 128-byte program descriptor.

The descriptor declares ABI and ISA compatibility, program format, required
capabilities and operation classes, code and argument locations, entry PC,
local-memory requirements, completion address, and flags. Reserved fields must
be zero.

Software writes `PROGRAM_DESC_ADDR_LO/HI`, then writes `DOORBELL.START`.
Doorbell is accepted only in `IDLE`. `DONE` and `FAULT` must first be cleared by
`CONTROL.CLEAR_TERMINAL`.

## Compatibility Validation

Before frontend start, hardware rejects:

- incorrect descriptor size or ABI major;
- unsupported ISA major, newer required ISA minor, or program format;
- unsupported capability or operation-class requirements;
- nonzero reserved fields or unknown flags;
- misaligned descriptor, code, argument, or completion addresses;
- zero/oversized/misaligned code and invalid entry PC;
- argument, stack, program-memory, or scratchpad bounds violations.

Failure enters `FAULT` without executing an instruction.

## Lifecycle

Exactly one lifecycle state is visible at a time:

| State | Meaning |
| ----- | ------- |
| `IDLE` | Ready for one doorbell. |
| `LOADING` | Descriptor, program, or arguments are being fetched. |
| `RUNNING` | The frontend is executing. |
| `HALTED` | Execution stopped at a precise boundary. |
| `DONE` | Sticky successful terminal state. |
| `FAULT` | Sticky fault terminal state. |
| `RESETTING` | Accepted work is quiescing before clean `IDLE`. |

`IRQ_PENDING` is an orthogonal status bit and may remain set while lifecycle is
`RESETTING`; it clears when reset quiescence completes.

Control writes are write-one pulses and may set only one defined bit. Invalid
or state-inappropriate writes return AXI-Lite `SLVERR`. In `RESETTING`, doorbell,
halt, resume, debug-step, and repeated reset are rejected.

## Halt, Debug, And Terminal State

- `HALT` requests a precise boundary from `RUNNING`.
- `RESUME` continues from `HALTED`.
- `DEBUG_STEP` executes one precise instruction from `HALTED`.
- `DEBUG_PC`, `FAULT_CODE`, cycle count, and retired instruction count provide
  architectural snapshots.
- `DONE` and `FAULT` remain stable until `CLEAR_TERMINAL` or software reset.

IRQ causes are sticky and independently enabled/cleared. Completion-record
writeback, when requested, finishes before terminal status and completion IRQ
are exposed.

## Software Reset

`CONTROL.SOFT_RESET` returns after the request is accepted, not after reset is
complete. Software observes `RESETTING` and uses `holon_npu_wait_idle()` to wait
for quiescence.

During quiescence:

- no new frontend fetch or engine issue is accepted;
- asserted AXI VALID and payload remain stable until handshake;
- accepted AXI and local-memory operations complete or drain;
- response errors do not replace the software reset request;
- architectural state, terminal state, IRQ, and performance state clear only
  after all components are quiescent.

External `aresetn` remains the only immediate abort mechanism.

## AXI4 Rules

Every read and write master obeys:

- INCR bursts;
- no more than 16 beats;
- stable VALID payload under backpressure;
- no burst crossing a 4 KiB boundary;
- complete response consumption before transaction ownership changes.

Descriptor, code, argument, DMA, and completion transfers are split as needed.
In particular, a descriptor at `...FF0` and completion record at `...FF0` are
legal and use multiple transactions.

## Program Faults

`SYSTEM_FAULT` has no software-provided fault operand. Its reserved immediate
must be zero and it always produces `EXPLICIT_PROGRAM_FAULT`. A nonzero reserved
immediate is an illegal instruction and produces `ILLEGAL_INSTRUCTION`.

Other faults preserve the categories in the generated ABI reference, including
descriptor validation, compatibility, alignment, local memory, vector config,
matrix issue, DMA request, and AXI response faults.

## Memory Visibility

HolonNPU is not cache coherent. Platform software owns physical allocation,
address translation, and required host cache maintenance for descriptors, code,
arguments, tensors, and completion records.

Program-visible results must reach system memory through DMA stores. Product
RTL exposes no test-only scratchpad read port.

## Public Software API

The C23 driver provides capability reads, descriptor initialization and
validation, submit, status/fault/performance reads, halt/resume/debug-step,
software reset, terminal clear, IRQ handling, wait-terminal, and wait-idle.

The C++26 runtime provides typed program construction and example program
builders over generated ISA encoders. These are software conveniences; generated
ABI/ISA headers remain the numeric source of truth.

## Compatibility Policy

ABI 3.0 is the only current product ABI in `master`. No ABI 2.0 headers,
descriptor paths, product targets, or compatibility aliases are retained. The
`v1.5` tag is the authoritative historical implementation.
