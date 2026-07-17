# HolonNPU 入门指南

HolonNPU 当前主线是一个可编程的整数/量化 NPU tile。Host 提交的不是
GEMM descriptor，而是 ABI 3.0 program descriptor；reference frontend 执行
Holon ISA 1.0 程序，并调度 DMA、vector、matrix 和同步操作。

旧的 descriptor-driven GEMM 实现只保存在 `v1.5` tag 中，不与当前产品
源码共同维护。

## 先理解运行流程

一次程序执行包含以下步骤：

1. 软件构造 Holon program image、argument block 和 program descriptor。
2. 软件通过 AXI-Lite 写 descriptor 地址并触发 doorbell。
3. loader 通过 AXI4 读取并验证 descriptor。
4. loader 将代码复制到 program memory，将参数复制到 data scratchpad。
5. control plane 从 `LOADING` 进入 `RUNNING`，reference frontend 开始取指。
6. frontend 执行控制流，并通过 interface issue DMA、vector、matrix 工作。
7. 程序使用 DMA STORE 将结果写回 system memory。
8. 若配置 completion record，硬件先完成并确认写回，再暴露 `DONE` 或
   `FAULT` 以及 IRQ。

`DONE`/`FAULT` 是 sticky 状态，下一次提交前必须执行
`CLEAR_TERMINAL`。软件 reset 会先进入可见的 `RESETTING`，排空已接受的
AXI/local-memory 工作后才回到 `IDLE`。

## 代码结构

| 路径 | 内容 |
| ---- | ---- |
| `spec/` | ABI、ISA、coverage baseline 的机器可检查来源。 |
| `rtl/common/` | AXI/AXI-Lite/valid-ready interface 和生成 package。 |
| `rtl/control/` | control registers、program loader、completion writer。 |
| `rtl/frontend/` | frontend interface 和 reference frontend。 |
| `rtl/localmem/` | program/data local memory 及仲裁。 |
| `rtl/dma/` | frontend-issued DMA fabric。 |
| `rtl/vector/` | integer/quant vector engine。 |
| `rtl/matrix/` | PE、systolic array、matrix micro-op engine。 |
| `rtl/integration/` | control plane、engine integration、canonical `npu_top`。 |
| `sim/rtl/` | 仅供 Verilator/C++ 使用的 flattened wrapper。 |
| `sim/model/` | C++26 architectural model。 |
| `sim/` | C++ testbench 和 typed coverage runtime。 |
| `include/` | 生成的 public headers 和 C++ runtime API。 |
| `sw/` | C23 driver 与 C++26 runtime 实现。 |
| `tools/` | schema generation、结构检查、coverage gate。 |

`rtl/` 中的模块必须能从 `npu_top` 产品图到达。`sim/rtl/` wrapper 只是测试
边界，不允许参与产品内部连接。program-level 测试也不能通过产品 test
probe 读取 scratchpad，而要执行 DMA STORE 后比较模拟 system memory。

## 环境要求

- CMake 4.0+
- Ninja
- Verilator
- 支持 C23 的 C compiler
- 支持 C++26 的 C++ compiler
- Python 3

确认入口：

```bash
cmake --list-presets
cmake --list-presets=build
cmake --list-presets=test
```

## 日常构建

Debug 配置与构建：

```bash
cmake --preset debug
cmake --build --preset debug --parallel 2
```

快速测试和 lint 分开执行：

```bash
ctest --preset debug --output-on-failure
ctest --preset lint --output-on-failure
```

优化后的完整回归：

```bash
cmake --preset regression
cmake --build --preset regression --parallel 2
ctest --preset regression --output-on-failure
```

覆盖率构建：

```bash
cmake --preset coverage
cmake --build --preset coverage --parallel 2
ctest --preset coverage --output-on-failure
python3 tools/check_coverage.py --build-dir build/coverage
```

`CMakePresets.json` 只固定 debug/regression/coverage build tree 和四个测试入口，
不为每个子系统增加 preset。单独构建或观察测试时直接使用 target/regex：

```bash
cmake --build --preset debug --target npu_dma_fabric_tb
ctest --preset debug -R '^npu_dma_fabric$' --verbose
```

## ABI 与 ISA 单一来源

不要直接修改以下生成文件：

- `rtl/common/npu_pkg.sv`
- `rtl/common/npu_isa_pkg.sv`
- `include/holon_npu_program.h`
- `include/holon_npu_isa.h`
- `docs/INTERFACE_REFERENCE.md`
- `docs/ISA_REFERENCE.md`

ABI 修改流程：

1. 先更新 `docs/INTERFACE.md` 和 ADR。
2. 修改 `spec/holon_npu_abi.json`。
3. 运行 `python3 tools/gen_abi.py`。
4. 运行 `python3 tools/gen_abi.py --check`。
5. 更新 RTL、driver、model、tests 和 coverage evidence。

ISA 修改流程：

1. 先更新 `docs/ISA.md` 和 ADR。
2. 修改 `spec/holon_npu_isa.json`。
3. 运行 `python3 tools/gen_isa.py`。
4. 运行 `python3 tools/gen_isa.py --check` 和
   `python3 tools/check_isa.py`。
5. 同步 decoder、encoder、model、RTL、runtime 和测试。

ABI generator 会同时读取 ISA schema，从而自动派生 ISA version 和
operation-class capability mask；不要在 ABI schema 中复制 operation-class
数字。

## RTL 阅读顺序

推荐按控制流阅读：

1. `rtl/common/npu_pkg.sv` 和 `npu_isa_pkg.sv`：生成常量和类型。
2. `rtl/common/npu_axi_lite_if.sv`、`npu_axi4_if.sv`：协议与通用 SVA。
3. `rtl/control/npu_control_regs.sv`：lifecycle、IRQ、reset、doorbell。
4. `rtl/control/npu_program_loader.sv`：descriptor 验证和 code/arg 加载。
5. `rtl/frontend/npu_frontend_if.sv` 与 `npu_reference_frontend.sv`：ISA
   执行及 engine issue。
6. `rtl/dma/npu_dma_fabric.sv`、`rtl/vector/npu_vector_engine.sv`、
   `rtl/matrix/npu_matrix_engine.sv`：执行引擎。
7. `rtl/integration/npu_frontend_tile.sv` 和 `npu_top.sv`：完整产品流程。

Matrix datapath 可继续深入阅读 `npu_pe_i8.sv` 和
`npu_systolic_array.sv`。B weight 驻留在 PE 中，A 水平传播，partial sum
垂直传播，architectural accumulator 为 INT32 wraparound。

## 软件入口

C23 driver 位于 `sw/holon_npu_driver.c` 和 `.h`，提供：

- capability/status/fault/perf 读取；
- descriptor 初始化、校验和提交；
- halt/resume/debug-step；
- IRQ clear；
- soft reset 与 `holon_npu_wait_idle()`；
- terminal wait/clear。

C++26 runtime 位于 `include/holon_npu_runtime.hpp` 和
`sw/holon_npu_runtime.cpp`，提供 typed program builder 和示例 kernel 构造。
编码常量来自生成的 `holon_npu_isa.h`。

## 验证方法

验证不是只依赖外部 C++ compare：

- product RTL 内直接使用 named native `assert property`；
- `npu_assert_fail` 证明 assertion 在 Verilator 中真实启用；
- C++ architectural model 给出 decode/retire/fault/engine semantics；
- AXI directed tests 覆盖 4 KiB split 和各 channel backpressure；
- reset tests 在 AR/R/AW/W/B stall 中验证 drain；
- typed functional coverage 只在 monitor/scoreboard 完成观察和校验时记录；
- checker 强制所有 named RTL `cover property` 非零；
- line/branch/toggle/expr 不得低于 checked baseline。

coverage run 开始前会删除旧 artifacts，并生成本轮
`expected_tests.txt`。因此缺失测试或旧 `.dat` 文件都不能掩盖问题。

## 常见修改检查表

修改 AXI/DMA：

- 检查 VALID/payload stability；
- 检查 burst length、alignment、4 KiB split；
- 添加 backpressure/error/reset-drain tests；
- 添加或更新 native SVA 和 functional event。

修改 vector/matrix：

- 先固定 ISA semantic；
- 同步 C++ model；
- 覆盖 tail、predicate、overflow、rounding、bounds 和 illegal mode；
- program-level 结果通过 DMA STORE 观察。

新增 RTL module：

- 使用 interface/modport 表达协议；
- 从 `npu_top` 产品图接入；
- 添加 lint、module test、assertion 和 coverage；
- 不在 `rtl/` 放 test wrapper。

## 提交前检查

```bash
python3 tools/gen_abi.py --check
python3 tools/gen_isa.py --check
python3 tools/check_isa.py
python3 tools/check_rtl_interface_usage.py
python3 tools/check_macro_policy.py
git diff --check
```

随后运行 debug、lint、regression 和 coverage 四个 gate。真实结果记录到
`docs/PROGRESS.md`，长期变更记录写入 `CHANGELOG.md`。
