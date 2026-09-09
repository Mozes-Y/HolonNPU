# Minimal Self-Hosted Transformer

This is an execution acceptance workload for ADR-0066, not an ISA extension,
RTL authorization, trained model, or performance claim.

## Program Contract

The initial specialization has three tokens, model width four, one causal
attention head, FFN width eight and vocabulary eight. Token IDs select learned
embedding rows in guest code. Positional embeddings are then added. The full
forward path is:

1. Token embedding plus position.
2. Independent Q, K and V projections.
3. QK transpose product scaled by `1/sqrt(4)`.
4. Causal masked stable softmax, row by row.
5. Probability times V, then attention output projection.
6. Residual addition and affine LayerNorm (`epsilon = 1e-5`).
7. FFN projection, ReLU and second projection.
8. Second residual and affine LayerNorm, then vocabulary logits.

Parameters and intermediate tensors use binary32. Matrix products use ordered
FMA, scalar reductions are ordered by lane, and all vector instructions specify
type, predicate and VL. The first program is a statically assembled shape
specialization; code-generation loops are not Host execution of tensor work.
Tokens and parameters remain runtime data. This does not yet demonstrate a
general dynamic-shape compiler or a large-model tile traversal strategy.

There is one boot and one execution run. Initial DMA loads the input image;
final DMA stores all stage tensors for observation. The environment only
services typed memory requests, never performs arithmetic between guest kernels.
The same assembled program must work at 16/64/128-byte vector capacities.

## Numerical Contract

Softmax subtracts each causal prefix maximum. Guest exp evaluates degree-ten
Taylor on `x/16`, then squares four times, using explicit VFMA/VMUL. Inputs are
clamped to `[-16, 0]`; masked columns are written as exact zero. Clamping a more
negative unmasked term to `-16` contributes at most `exp(-16)` before row
normalization. This is a workload approximation, not a change to ISA arithmetic.

The independent reference uses double precision, `std::exp`, direct matrix
loops, population variance, affine LayerNorm and ReLU. Every named stage is
checked with `abs(error) <= 2e-4 + 2e-4 * abs(reference)`, plus finite outputs,
exact causal zeros and normalized probability rows. The tolerance is fixed
before running the tests, not fitted to observed errors. Failures report seed,
capacity, stage, element, expected and actual values.

Directed zero/constant/near-constant inputs and large Q/K weights accompany
deterministic random weights and tokens. The large-score fixture must actually
reach an exponent argument below -16. Larger dimensions, broader near-singular
numerical stress, compiled dynamic guest loops and external-memory timing
remain follow-on evidence, not implied by this specialization.

## Verification Entry

```sh
cmake --build --preset debug --target holon_npu_transformer_test --parallel 2
ctest --preset debug -R '^holon_npu_transformer$' --verbose
```

Each execution reports seed, vector capacity, stage count, retired instructions
and maximum absolute error. Current fixtures use seeds 0, 1, 3, 4, 17, 42, 123,
2026, `0x484f4c` and `0xffffffff` across three capacities, for 30 executions.
