# HolonNPU 新手入门教程

本文是一份面向新手的项目导览。目标是从最基础的概念开始，带你一步一步理解
`HolonNPU` 的设计、代码结构、运行方式、验证方法，以及以后如何安全地上手修改
这个项目。

如果你现在已经不太记得项目各部分的设计情况，可以按本文的顺序重新进入项目。

## 1. 项目一句话介绍

`HolonNPU` 是一个用 SystemVerilog 实现的 v1 INT8 GEMM NPU。

它的主要功能是加速矩阵乘法：

```text
C[M, N] = A[M, K] x B[K, N]
```

当前 v1 的固定范围是：

- A 和 B 是 signed INT8。
- C 是 signed INT32。
- 核心计算单元是参数化 `16x16` systolic array。
- CPU 通过 AXI-Lite 寄存器控制 NPU。
- NPU 通过 AXI4 DMA 主动访问系统内存。
- 软件侧提供最小 C driver。
- 验证使用 Verilator + C++26 testbench + CTest。

v1 不做：

- vector engine
- BF16
- FP8
- convolution
- softmax
- LayerNorm
- GELU
- graph scheduler
- 多 descriptor queue
- 多 context
- IOMMU / 地址转换
- 多 NPU tile 扩展

这些未来功能必须先进入 `docs/ROADMAP.md` 和 `docs/DECISIONS.md`，不能直接混进
v1。

## 2. 你应该先知道的三个核心概念

### 2.1 AXI-Lite：CPU 控制 NPU 的寄存器接口

AXI-Lite 是 CPU 访问 NPU 控制寄存器的接口。

CPU 通过 AXI-Lite 做这些事：

- 读取设备 ID 和 capability。
- 写 descriptor 地址。
- 写 doorbell 启动 NPU。
- 读取 busy/done/error 状态。
- 开关 IRQ。
- 清 done/error/IRQ/performance counter。
- 触发 soft reset。

对应 RTL：

- `rtl/control/npu_control_regs.sv`

对应文档：

- `docs/INTERFACE.md` 的 AXI-Lite register map。

### 2.2 AXI4 DMA：NPU 主动访问系统内存

AXI4 是 NPU 作为 bus master 访问系统内存的接口。

NPU 通过 AXI4 做这些事：

- 读取 GEMM descriptor。
- 读取 A 矩阵。
- 读取 B 矩阵。
- 写回 C 矩阵。

对应 RTL：

- `rtl/dma/npu_axi4_read_dma.sv`
- `rtl/dma/npu_axi4_write_dma.sv`

v1 DMA 约束：

- AXI4 data width 是 128 bit，也就是 16 byte。
- 地址必须 16-byte aligned。
- byte count 必须非零且是 16 的倍数。
- 一个 burst 最多 16 beats，也就是最多 256 bytes。
- 每个 DMA engine v1 只支持一个 outstanding burst。

### 2.3 Descriptor：CPU 放在内存里的 NPU 命令

Descriptor 是一段 128 字节的数据结构。CPU 把它放在系统内存中，然后告诉 NPU
descriptor 的物理地址。

Descriptor 里包含：

- 操作类型，目前只支持 GEMM。
- M/N/K。
- A/B/C 矩阵的系统内存地址。
- A/B/C 每一行的 stride。
- IRQ flag。
- 是否在启动时清 performance counter。
- 保留字段。

对应头文件：

- `include/holon_npu_desc.h`

对应 RTL 解析：

- `rtl/command/npu_command_processor.sv`

对应 ABI 文档：

- `docs/INTERFACE.md` 的 GEMM Descriptor ABI。

## 3. 一次完整 GEMM 是怎么跑起来的

下面是一次完整 NPU GEMM 的执行流程。

### 3.1 软件准备数据

软件先在系统内存中准备：

- A matrix，signed INT8，row-major。
- B matrix，signed INT8，row-major。
- C matrix 输出空间，signed INT32，row-major。
- 一个 128-byte GEMM descriptor。

所有地址和 stride 必须满足 v1 对齐要求：

- descriptor 地址 16-byte aligned。
- A/B/C base address 16-byte aligned。
- A/B/C row stride 16-byte aligned。

### 3.2 软件写 AXI-Lite 寄存器

CPU 写：

```text
DESC_ADDR_LO
DESC_ADDR_HI
DOORBELL.START = 1
```

这些寄存器定义在：

- `docs/INTERFACE.md`
- `include/holon_npu_regs.h`
- `rtl/common/npu_pkg.sv`

### 3.3 Control plane 启动 command processor

`npu_control_regs.sv` 收到合法 doorbell 后：

- 设置 busy。
- 清旧的 done/error。
- 发出一拍 `command_start_o`。
- 把 descriptor 地址送给 command processor。

### 3.4 Command processor 读取并检查 descriptor

`npu_command_processor.sv` 做这些事：

1. 用 read DMA 从 AXI4 读取 128-byte descriptor。
2. 按 `docs/INTERFACE.md` 解码字段。
3. 检查：
   - descriptor size 是否等于 128。
   - version 是否为 2。
   - opcode 是否为 GEMM。
   - flags 是否只有合法位。
   - M/N/K 是否非零且不超过 v1 限制。
   - A/B/C 地址是否对齐。
   - stride 是否对齐且足够大。
   - 所有 reserved 字段是否为 0。
4. 如果合法，发出 GEMM command。
5. 如果不合法，进入 error，并给出明确 error code。

### 3.5 GEMM accelerator 执行 tile 调度

`npu_gemm_accelerator.sv` 是 GEMM datapath 的主状态机。

它按 `16x16x16` tile 遍历：

- M 维度按 16 行分块。
- N 维度按 16 列分块。
- K 维度按 16 分块累加。

每个 output tile 的大致过程：

1. 清当前 C tile accumulator。
2. load A tile row。
3. load B tile row，并写入 PE 里的 stationary weight register。
4. 让 systolic array 计算，输出 streamed C partial sums。
5. 如果 K 还有后续 tile，继续在 C tile accumulator 里累加。
6. K 全部完成后，把 C tile 写回内存。
7. 进入下一个 M/N tile。

### 3.6 Systolic array 做矩阵乘法

矩阵核心文件：

- `rtl/matrix/npu_pe_i8.sv`
- `rtl/matrix/npu_systolic_array.sv`

v1.1 使用 B-weight-stationary dataflow：

- 每个 PE 保存一个 B weight。
- A 从左侧按 wavefront 输入，向右传播。
- zero psum 从顶部输入，向下穿过 K lanes。
- 每个 PE 做 signed INT8 乘法，然后把结果加到输入 psum。
- C partial sum 从阵列底部流出，再累加到 C tile accumulator。
- 非完整 tile 通过 mask 禁用越界 row/col/K。

### 3.7 C 结果写回系统内存

GEMM accelerator 用 write DMA 写回 C：

- C 是 signed INT32。
- 一个 AXI beat 是 128 bit。
- 一个 beat 可以写 4 个 INT32。
- 如果 N 不是 4 的倍数，最后一个 chunk 内部无效 lane 写 0。
- 如果 N 不是 16 的倍数，只写活跃 C chunk。

### 3.8 完成、报错、IRQ

成功时：

- control plane 设置 `STATUS.DONE`。
- 如果 descriptor 设置了 `IRQ_ON_DONE`，设置 `IRQ_STATUS.DONE_IRQ`。
- 如果 IRQ enable 打开，则 `irq_o` 拉高。

失败时：

- control plane 设置 `STATUS.ERROR`。
- `ERROR_CODE` 给出具体错误。
- 如果允许 error IRQ，则设置 `IRQ_STATUS.ERROR_IRQ`。

软件之后通过 `CLEAR` 或 `IRQ_STATUS` 清状态。

## 4. 目录结构总览

项目主要目录：

```text
docs/       路线图、架构、ABI、验证计划、进度、架构决策记录
include/    公共 C ABI 头文件
rtl/        SystemVerilog RTL
sim/        Verilator C++26 testbench
sw/         最小 C driver
tests/      host-side driver test
tools/      项目工具，例如 ABI consistency checker
```

建议你先熟悉这些入口文件：

```text
README.md
docs/ARCHITECTURE.md
docs/INTERFACE.md
docs/VERIFICATION.md
rtl/integration/npu_top.sv
sim/top_tb.cpp
```

## 5. 文档体系怎么读

### 5.1 ROADMAP

文件：

- `docs/ROADMAP.md`

作用：

- 定义 v1 到 v5 的路线图。
- 定义 v1 Phase 0 到 Phase 11。
- 定义每个阶段的目标、交付物、验收标准。

开发前必须先看它，避免把未来功能混进当前版本。

### 5.2 PROGRESS

文件：

- `docs/PROGRESS.md`

作用：

- 记录真实进度。
- 记录每个阶段实际跑过的命令。
- 记录测试结果和已知限制。

这是判断项目当前状态的最直接文档。

### 5.3 ARCHITECTURE

文件：

- `docs/ARCHITECTURE.md`

作用：

- 描述整体架构。
- 解释 control plane、command processor、DMA、scratchpad、matrix engine、
  software driver。

这是理解项目设计的主文档。

### 5.4 INTERFACE

文件：

- `docs/INTERFACE.md`

作用：

- 定义 v1 ABI。
- 定义 AXI-Lite register map。
- 定义 descriptor layout。
- 定义 error code。
- 定义 IRQ 语义。
- 定义软件 API 合约。

任何 RTL、driver、test 都必须和它一致。

### 5.5 VERIFICATION

文件：

- `docs/VERIFICATION.md`

作用：

- 定义验证策略。
- 定义每个 phase 的测试 checklist。
- 定义 release checklist。

如果你改模块，应该先看这里对应模块需要满足哪些测试。

### 5.6 DECISIONS

文件：

- `docs/DECISIONS.md`

作用：

- 记录重要架构决策。
- 解释为什么选这个方案。
- 记录 rejected alternatives。

当你想改架构时，先看这里，避免重复踩以前已经分析过的坑。

## 6. RTL 代码学习路线

下面按推荐学习顺序介绍 RTL。

## 6.1 Common RTL

目录：

```text
rtl/common/
```

重要文件：

```text
npu_pkg.sv
npu_fifo.sv
npu_skid_buffer.sv
npu_register_slice.sv
npu_vr_if.sv
npu_axi_lite_if.sv
npu_axi4_if.sv
```

这些 interface 不是占位文件。产品 RTL 内部以它们作为协议边界：

- valid-ready 数据流使用 `npu_vr_if.source/sink`。
- AXI-Lite control plane 使用 `npu_axi_lite_if.master/slave`。
- AXI4 DMA 和顶层仲裁使用 `npu_axi4_if` 的 read/write modport。

只有 C++/Verilator 测试便利层使用 flattened `*_test_wrapper.sv`。这些
wrapper 不属于核心架构，产品 RTL 内部不能实例化它们。

### npu_pkg.sv

这是最重要的公共包。

里面定义：

- ABI major/minor。
- descriptor size/alignment。
- register offset。
- capability reset value。
- opcode。
- descriptor flag。
- error code。

这些值必须和 C header 一致：

- `include/holon_npu_regs.h`
- `include/holon_npu_desc.h`

一致性由这个工具检查：

```sh
python3 tools/check_abi_consistency.py
```

### FIFO / skid buffer / register slice

这些是 valid-ready 基础模块。

学习重点：

- valid-ready 怎么握手。
- backpressure 怎么传播。
- reset 后状态如何初始化。
- C++ testbench 如何逐周期验证它们。

对应测试：

- `sim/common_tb.cpp`

## 6.2 Control Plane

文件：

```text
rtl/control/npu_control_regs.sv
```

它是 AXI-Lite slave。

主要功能：

- 实现 register map。
- 接收独立 AW/W channel。
- 对非法访问返回 `SLVERR`。
- 保存 descriptor 地址。
- 接收 doorbell。
- 维护 busy/done/error。
- 管理 IRQ enable/status。
- 管理 performance counter。
- 发出 soft reset 和 clear perf pulse。

学习时建议同时打开：

- `docs/INTERFACE.md`
- `include/holon_npu_regs.h`
- `sim/control_tb.cpp`

重点观察：

- AW 和 W 可以同周期到，也可以不同周期到。
- 写只读寄存器要返回 `SLVERR`。
- doorbell while busy 要返回 `SLVERR`。
- `CLEAR.DONE` 和 `CLEAR.ERROR` 会清对应 IRQ status。
- `STATUS.IDLE` 在 terminal done/error 时也是 1，因为没有 active descriptor。

## 6.3 Command Processor

文件：

```text
rtl/command/npu_command_processor.sv
```

它是控制路径和数据路径之间的桥。

主要功能：

- 收到 start。
- 发起 descriptor read DMA。
- 收集 8 个 128-bit beat，刚好 128 byte。
- 解码 descriptor。
- 做合法性检查。
- 如果合法，输出 GEMM command。
- 如果非法，输出 error code。

常见错误码：

- `ERR_INVALID_DESC_SIZE`
- `ERR_INVALID_DESC_VERSION`
- `ERR_INVALID_OPCODE`
- `ERR_INVALID_FLAGS`
- `ERR_UNSUPPORTED_ALIGNMENT`
- `ERR_RESERVED_NONZERO`
- `ERR_DIMENSION_ZERO`
- `ERR_DIMENSION_UNSUPPORTED`
- `ERR_AXI_READ`

对应测试：

- `sim/command_tb.cpp`

测试覆盖：

- 合法 descriptor。
- 非法 opcode/version/size/flags。
- reserved field 非零。
- descriptor fetch AXI error。
- deterministic descriptor fuzz。

## 6.4 DMA

目录：

```text
rtl/dma/
```

重要文件：

```text
npu_axi4_read_dma.sv
npu_axi4_write_dma.sv
npu_read_dma_test_top.sv
npu_write_dma_test_top.sv
```

### Read DMA

`npu_axi4_read_dma.sv` 做：

- 检查地址和长度对齐。
- 发 AR。
- 接收 R data。
- 通过 valid-ready 输出 read data。
- 支持多 burst。
- 遇到 read response error 时报告 `ERR_AXI_READ`。

一个关键设计点：

如果 AXI read 在非最后一个 beat 返回错误，read DMA 不能立即丢掉 `RREADY`。
它必须继续 drain 到 `RLAST`，否则顶层 read arbiter 无法释放 read owner。

这部分设计记录在：

- `docs/DECISIONS.md` 的 ADR-0016。

### Write DMA

`npu_axi4_write_dma.sv` 做：

- 检查地址和长度对齐。
- 发 AW。
- 接收 input stream。
- 发 W data。
- 等待 B response。
- 遇到 write response error 时报告 `ERR_AXI_WRITE`。

对应测试：

- `sim/read_dma_tb.cpp`
- `sim/write_dma_tb.cpp`

## 6.5 Matrix Engine

目录：

```text
rtl/matrix/
```

重要文件：

```text
npu_pe_i8.sv
npu_systolic_array.sv
npu_systolic_array_test_top.sv
```

### PE

PE 是 processing element。

核心操作：

```text
acc += signed_int8_a * signed_int8_b
```

输出是 signed INT32。

### Systolic Array

`npu_systolic_array.sv` 把 PE 组成二维阵列。

v1 设计：

- B weight 先加载并驻留在 PE 内。
- A 从左边按 wavefront 进入并向右传播。
- zero psum 从顶部进入并向下传播。
- C partial sum 从底部输出。
- B-weight-stationary dataflow。

对于 `16x16x16` tile，需要完整 wavefront。

对应测试：

- `sim/pe_tb.cpp`
- `sim/array_tb.cpp`

测试覆盖：

- 1x1。
- 16x16x16。
- 17x19x23。
- 正数、负数、零、溢出边界。
- C++ golden model 对比。

## 6.6 Datapath / Scratchpad

目录：

```text
rtl/datapath/
```

重要文件：

```text
npu_tile_mask.sv
npu_banked_scratchpad.sv
npu_i8_tile_buffer.sv
npu_c_accum_buffer.sv
npu_gemm_tile_scratchpad.sv
npu_ping_pong_ctrl.sv
npu_tiling_datapath_test_top.sv
```

这些模块负责：

- reusable tile mask、banked scratchpad、A/B tile buffer、C buffer 和
  ping-pong schedule infrastructure。
- tiling test top 中的 A/B/C buffer、banking、mask 和 schedule 行为验证。
- product-active v1.1 GEMM datapath 中的 A wavefront、K/column masks 和 zero
  psum timing。

其中集成进 GEMM accelerator 的关键模块是：

```text
rtl/datapath/npu_gemm_tile_scratchpad.sv
```

在 v1.1 product top 中，`npu_gemm_tile_scratchpad.sv` 不再保存 B tile，也
不作为 C accumulator。B tile row 由 GEMM scheduler 直接写入 PE 的
stationary weight registers，C partial sums 在 `npu_gemm_accelerator.sv`
内部累加后写回。`npu_i8_tile_buffer.sv`、`npu_c_accum_buffer.sv` 和
`npu_ping_pong_ctrl.sv` 仍然作为 Phase 5 reusable infrastructure 被 lint 和
tiling test 覆盖。

对应测试：

- `sim/tiling_tb.cpp`

## 6.7 GEMM Accelerator

文件：

```text
rtl/integration/npu_gemm_accelerator.sv
```

这是 GEMM 数据路径的主状态机。

它连接：

- command input。
- read DMA。
- write DMA。
- GEMM tile scratchpad。
- systolic array。
- stage/debug output。

典型状态包括：

- idle
- tile clear
- load A
- load B
- compute
- K advance
- store
- next tile
- done
- error

它不关心 AXI-Lite register，也不重新解析 descriptor。它只接收 command processor
已经解码好的 GEMM command。

对应测试：

- `sim/gemm_tb.cpp`

测试覆盖：

- GEMM 正确性。
- reset-in-flight。
- AXI read error。
- AXI write error。

## 6.8 Product Top

文件：

```text
rtl/integration/npu_top.sv
```

这是项目最重要的顶层模块。

它连接：

- AXI-Lite control。
- command processor。
- GEMM accelerator。
- shared AXI4 read channel。
- AXI4 write channel。
- IRQ。
- soft reset。
- performance counter clear。

### 顶层 read arbitration

descriptor fetch 和 GEMM tensor read 都需要 AXI4 read。

v1 只有一个 external AXI read channel，所以 `npu_top.sv` 做仲裁：

- 如果 command processor 发 AR，优先 descriptor fetch。
- 如果 GEMM accelerator 发 AR，处理 tensor read。
- 一旦某个 client 的 AR 被接受，top 记录 read owner。
- 后续 R beats 都送回这个 owner。
- 直到 `RLAST` 后释放 owner。

这就是为什么 read DMA 遇到 read error 时必须 drain 到 `RLAST`。

对应测试：

- `sim/top_tb.cpp`

这是最重要的集成测试。

## 7. 软件侧代码

目录：

```text
include/
sw/
tests/
```

## 7.1 Public ABI Headers

文件：

```text
include/holon_npu_regs.h
include/holon_npu_desc.h
```

### holon_npu_regs.h

定义：

- reset constants。
- register offset。
- status bit。
- IRQ bit。
- clear bit。
- hardware error code。

### holon_npu_desc.h

定义：

- descriptor struct。
- descriptor config struct。
- opcode。
- descriptor flags。
- static assert 检查字段 offset 和 size。

`holon_npu_gemm_desc_t` 必须正好 128 bytes。

## 7.2 C Driver

文件：

```text
sw/holon_npu_driver.h
sw/holon_npu_driver.c
```

driver 提供：

- `holon_npu_init`
- `holon_npu_get_caps`
- `holon_npu_build_gemm_desc`
- `holon_npu_submit`
- `holon_npu_poll`
- `holon_npu_wait`
- `holon_npu_error`
- `holon_npu_clear`
- `holon_npu_read_perf`

driver 做本地参数检查，例如：

- descriptor 地址是否对齐。
- tensor 地址是否对齐。
- stride 是否对齐。
- M/N/K 是否非零。
- flags 是否合法。

driver 不做：

- cache flush。
- 物理内存分配。
- OS IRQ 注册。
- 虚拟地址到物理地址转换。

这些由平台层负责。

## 7.3 典型软件调用流程

示例：

```c
holon_npu_dev_t dev;
holon_npu_init(&dev, mmio_base);

holon_npu_gemm_config_t cfg = {
    .m = M,
    .n = N,
    .k = K,
    .flags = HOLON_NPU_DESC_FLAG_IRQ_ON_DONE |
             HOLON_NPU_DESC_FLAG_IRQ_ON_ERROR |
             HOLON_NPU_DESC_FLAG_CLEAR_PERF_ON_START,
    .a_addr = a_pa,
    .b_addr = b_pa,
    .c_addr = c_pa,
    .a_row_stride_bytes = a_stride,
    .b_row_stride_bytes = b_stride,
    .c_row_stride_bytes = c_stride,
};

holon_npu_gemm_desc_t desc;
holon_npu_build_gemm_desc(&desc, &cfg);

/* 平台代码负责把 desc 放到 DMA 可见内存并处理 cache */
holon_npu_submit(&dev, desc_pa);

holon_npu_status_t status;
holon_npu_wait(&dev, timeout_polls, &status);

if (status.error) {
    uint32_t error_code;
    holon_npu_error(&dev, &error_code);
}
```

对应测试：

- `tests/driver_test.cpp`

## 8. 构建和运行

## 8.1 配置

```sh
cmake --preset debug
```

项目保留 `CMakePresets.json`，但只用它固定必要工作流：

- `build/debug` 和 `build/regression` 两个 build tree。
- `Debug` 和 `RelWithDebInfo` 两种构建类型。
- `Ninja` generator。
- `debug`、`lint`、`regression` 三个 CTest 入口。

不要把子系统快捷命令或架构版本信息放进 preset。单项构建用 `--target`，单项测试用
`ctest -R`。

## 8.2 构建

```sh
cmake --build --preset debug --parallel 2
```

## 8.3 运行快速 debug 测试

```sh
ctest --preset debug -j 2 --output-on-failure
```

`debug` 测试预设面向本地快速反馈：它会运行 static check、smoke、unit、module
和 driver 测试，但会排除 RTL lint 与标记为 slow 的系统级仿真。

## 8.4 运行 RTL lint

```sh
ctest --preset lint -j 2 --output-on-failure
```

如果只想构建 lint 聚合 target，可以运行：

```sh
cmake --build --preset debug --target lint --parallel 2
```

## 8.5 运行完整 regression

```sh
cmake --preset regression
cmake --build --preset regression --parallel 2
ctest --preset regression -j 2 --output-on-failure
```

regression 使用独立的 `RelWithDebInfo` 构建目录 `build/regression`。构建阶段只
编译优化后的仿真目标，测试阶段由 CTest preset 单独调度。

## 8.6 单独跑某类测试

只跑顶层：

```sh
cmake --build --preset debug --target npu_top_tb
ctest --preset regression -R npu_top --verbose
```

只跑 GEMM：

```sh
cmake --build --preset debug --target npu_gemm_tb
ctest --preset regression -R npu_gemm --verbose
```

只跑 DMA：

```sh
cmake --build --preset debug --target npu_read_dma_tb npu_write_dma_tb
ctest --preset debug -R 'npu_read_dma|npu_write_dma' --verbose
```

原则是：preset 只表达常用构建/测试模式；单项构建使用 CMake 原生
`--target`，单项测试使用 CTest 原生 `-R` 和 `--verbose`。

## 9. Testbench 学习路线

目录：

```text
sim/
```

推荐阅读顺序：

1. `sim/smoke_tb.cpp`
2. `sim/common_tb.cpp`
3. `sim/pe_tb.cpp`
4. `sim/array_tb.cpp`
5. `sim/read_dma_tb.cpp`
6. `sim/write_dma_tb.cpp`
7. `sim/control_tb.cpp`
8. `sim/command_tb.cpp`
9. `sim/gemm_tb.cpp`
10. `sim/top_tb.cpp`

## 9.1 smoke_tb.cpp

最简单的 Verilator 测试。

用于确认：

- CMake 能调用 Verilator。
- C++ testbench 能驱动 RTL。
- 基础仿真流程可用。

## 9.2 common_tb.cpp

验证 common RTL：

- FIFO。
- skid buffer。
- register slice。

重点学习 valid-ready 握手。

## 9.3 pe_tb.cpp / array_tb.cpp

验证矩阵核心。

重点学习：

- C++ golden model。
- signed INT8 到 signed INT32。
- wraparound 语义。
- partial tile mask。

## 9.4 read_dma_tb.cpp / write_dma_tb.cpp

验证 DMA。

重点学习：

- C++ memory model 如何模拟 AXI。
- AXI burst 如何计数。
- response error 如何注入。
- 非对齐输入如何被拒绝。

## 9.5 control_tb.cpp

验证 AXI-Lite control register。

重点学习：

- register read/write。
- AW/W skew。
- read-only write rejection。
- doorbell busy rejection。
- IRQ status clear。
- soft reset。

## 9.6 command_tb.cpp

验证 descriptor fetch/decode。

重点学习：

- descriptor memory layout。
- descriptor validation。
- invalid descriptor fuzz。

## 9.7 gemm_tb.cpp

验证 GEMM accelerator，不经过 AXI-Lite register top。

重点学习：

- tile GEMM 调度。
- A/B/C memory model。
- reset-in-flight。
- read/write error injection。

## 9.8 top_tb.cpp

这是最重要的系统级测试。

它覆盖：

- AXI-Lite register programming。
- descriptor fetch。
- GEMM execution。
- AXI4 read/write memory model。
- IRQ/status。
- invalid descriptor。
- descriptor read error recovery。
- GEMM read error。
- GEMM write error。
- reset-in-flight。
- `1x1x1`、`16x16x16`、`17x19x23`、`64x64x64`。

如果你只想看“这个 NPU 完整怎么跑”，先看 `sim/top_tb.cpp`。

## 10. CMake target 怎么对应模块

常用 target：

```text
npu_smoke_tb
npu_common_tb
npu_pe_tb
npu_array_tb
npu_tiling_tb
npu_control_tb
npu_read_dma_tb
npu_write_dma_tb
npu_command_tb
npu_gemm_tb
npu_top_tb
holon_npu_driver_test
```

常用 lint target：

```text
common_rtl_lint
matrix_rtl_lint
datapath_rtl_lint
control_rtl_lint
dma_rtl_lint
command_rtl_lint
integration_rtl_lint
lint
```

快速测试入口：

```sh
ctest --preset debug -j 2 --output-on-failure
```

完整 release gate 还需要单独运行 lint 和 regression：

```sh
ctest --preset lint -j 2 --output-on-failure
cmake --preset regression
cmake --build --preset regression --parallel 2
ctest --preset regression -j 2 --output-on-failure
```

## 11. ABI 一致性

项目里有四处必须保持一致：

```text
docs/INTERFACE.md
rtl/common/npu_pkg.sv
include/holon_npu_regs.h
include/holon_npu_desc.h
```

如果你改了 register offset、error code、descriptor flag、descriptor size 等，
必须同步修改这些文件。

工具：

```sh
python3 tools/check_abi_consistency.py
```

CTest 中也有：

```text
abi_consistency
```

## 12. 常见开发任务示例

## 12.1 如果你想改 AXI-Lite register

先看：

```text
docs/INTERFACE.md
rtl/control/npu_control_regs.sv
sim/control_tb.cpp
include/holon_npu_regs.h
rtl/common/npu_pkg.sv
```

推荐流程：

1. 先改 `docs/INTERFACE.md`。
2. 更新 `docs/DECISIONS.md`，如果是架构级变化。
3. 改 `npu_pkg.sv`。
4. 改 `holon_npu_regs.h`。
5. 改 `npu_control_regs.sv`。
6. 改或新增 `sim/control_tb.cpp` 测试。
7. 跑：

```sh
python3 tools/check_abi_consistency.py
cmake --build --preset debug --target npu_control_tb
ctest --preset debug -R npu_control --verbose
ctest --preset lint -j 2 --output-on-failure
```

## 12.2 如果你想改 descriptor ABI

先看：

```text
docs/INTERFACE.md
include/holon_npu_desc.h
rtl/common/npu_pkg.sv
rtl/command/npu_command_processor.sv
sim/command_tb.cpp
```

注意：

- descriptor size 当前固定 128 bytes。
- reserved 字段必须为 0。
- public C struct offset 必须和文档一致。
- command processor 解码 offset 必须一致。

跑：

```sh
python3 tools/check_abi_consistency.py
cmake --build --preset debug --target npu_command_tb holon_npu_driver_test
ctest --preset debug -R 'npu_command|holon_npu_driver|abi_consistency' --verbose
```

## 12.3 如果你想改 DMA

先看：

```text
docs/INTERFACE.md
rtl/dma/npu_axi4_read_dma.sv
rtl/dma/npu_axi4_write_dma.sv
sim/read_dma_tb.cpp
sim/write_dma_tb.cpp
```

跑：

```sh
cmake --build --preset debug --target npu_read_dma_tb npu_write_dma_tb
ctest --preset debug -R 'npu_read_dma|npu_write_dma' --verbose
ctest --preset lint -j 2 --output-on-failure
```

特别注意：

- read error 必须 drain 到 `RLAST`。
- 不要破坏 top-level read owner release。
- 不要引入 v1 不支持的 multiple outstanding。

## 12.4 如果你想改 systolic array

先看：

```text
rtl/matrix/npu_pe_i8.sv
rtl/matrix/npu_systolic_array.sv
sim/pe_tb.cpp
sim/array_tb.cpp
rtl/datapath/npu_gemm_tile_scratchpad.sv
```

跑：

```sh
cmake --build --preset debug --target npu_pe_tb npu_array_tb npu_gemm_tb npu_top_tb
ctest --preset debug -R 'npu_pe|npu_array' --verbose
ctest --preset regression -R 'npu_gemm|npu_top' --verbose
ctest --preset lint -j 2 --output-on-failure
```

特别注意：

- A wavefront、B stationary weight load、psum top-to-bottom 契约不能随便改。
- valid/mask 传播必须保持正确。
- `17x19x23` 这种非整 tile case 很容易暴露 bug。

## 12.5 如果你想改 GEMM scheduler

先看：

```text
rtl/integration/npu_gemm_accelerator.sv
rtl/datapath/npu_gemm_tile_scratchpad.sv
sim/gemm_tb.cpp
sim/top_tb.cpp
```

跑：

```sh
cmake --build --preset debug --target npu_gemm_tb npu_top_tb
ctest --preset regression -R 'npu_gemm|npu_top' --verbose
ctest --preset lint -j 2 --output-on-failure
```

特别注意：

- M/N/K 三层 tile 遍历。
- K tile 之间要累加同一个 output tile。
- M/N tile 切换时才清 C accumulator。
- C writeback 只写有效 chunk。

## 12.6 如果你想改 software driver

先看：

```text
include/holon_npu_regs.h
include/holon_npu_desc.h
sw/holon_npu_driver.c
sw/holon_npu_driver.h
tests/driver_test.cpp
```

跑：

```sh
cmake --build --preset debug --target holon_npu_driver_test
ctest --preset debug -R holon_npu_driver --verbose
```

## 13. Debug 方法

推荐 debug 流程：

1. 先用最小 CTest 复现。
2. 确认失败属于哪个层级：
   - register/control
   - command/descriptor
   - DMA
   - scratchpad/tile mask
   - matrix engine
   - GEMM scheduler
   - software driver
3. 看对应 testbench 的 error print。
4. 对照 C++ golden model。
5. 必要时打开 wave。
6. 修复后先跑局部测试。
7. 再跑快速 `ctest --preset debug -j 2`。
8. 最后跑 `ctest --preset lint -j 2` 和 regression。

## 14. 从零重新理解项目的推荐路线

如果你现在完全陌生，建议分四轮学习。

### 第一轮：理解整体

读：

```text
README.md
docs/ARCHITECTURE.md
docs/INTERFACE.md
rtl/integration/npu_top.sv
sim/top_tb.cpp
```

目标：

- 知道 NPU 从 CPU doorbell 到 C 写回的完整流程。
- 知道 AXI-Lite、descriptor、AXI4 DMA 各自做什么。

### 第二轮：理解控制路径

读：

```text
rtl/control/npu_control_regs.sv
rtl/command/npu_command_processor.sv
include/holon_npu_regs.h
include/holon_npu_desc.h
sw/holon_npu_driver.c
sim/control_tb.cpp
sim/command_tb.cpp
```

目标：

- 理解寄存器。
- 理解 descriptor。
- 理解启动、完成、错误、IRQ。

### 第三轮：理解数据路径

读：

```text
rtl/dma/npu_axi4_read_dma.sv
rtl/dma/npu_axi4_write_dma.sv
rtl/datapath/npu_gemm_tile_scratchpad.sv
rtl/matrix/npu_systolic_array.sv
rtl/integration/npu_gemm_accelerator.sv
sim/gemm_tb.cpp
```

目标：

- 理解 A/B 怎么从内存加载。
- 理解 tile 怎么进入 systolic array。
- 理解 C 怎么写回。

### 第四轮：理解验证体系

读：

```text
docs/VERIFICATION.md
sim/read_dma_tb.cpp
sim/write_dma_tb.cpp
sim/command_tb.cpp
sim/gemm_tb.cpp
sim/top_tb.cpp
tests/driver_test.cpp
```

目标：

- 知道每个模块如何被验证。
- 知道如何新增测试。
- 知道 release gate 要求。

## 15. 最小上手任务建议

如果你想练手，不建议一开始改核心 GEMM。

推荐从这些小任务开始：

1. 在 `sim/control_tb.cpp` 里新增一个非法寄存器访问测试。
2. 在 `sim/read_dma_tb.cpp` 里新增一个 alignment error case。
3. 在 `tests/driver_test.cpp` 里新增一个 driver argument validation case。
4. 在 `docs/INTERFACE.md` 中补充一个说明性段落，然后确认不影响 ABI。
5. 在 `sim/top_tb.cpp` 中新增一个小尺寸 GEMM case，比如 `3x5x7`。

每次练习都遵守：

```sh
cmake --build --preset debug --parallel 2
ctest --preset debug -j 2 --output-on-failure
ctest --preset lint -j 2 --output-on-failure
python3 tools/check_rtl_interface_usage.py
```

## 16. 项目当前最重要的边界

请始终记住：

- `docs/INTERFACE.md` 是 ABI 权威。
- `docs/ROADMAP.md` 是范围权威。
- `docs/PROGRESS.md` 是实际进度权威。
- `docs/DECISIONS.md` 是架构决策历史。
- `rtl/common/npu_pkg.sv` 和 `include/*.h` 必须保持 ABI 一致。
- `npu_top.sv` 是 SoC 集成入口和产品 pin boundary；其内部实例化
  interface-native `npu_top_core`。
- `*_test_wrapper.sv` 只服务 C++/Verilator 测试，不能作为产品 RTL 内部连接
  层。
- `sim/top_tb.cpp` 是最重要的系统级行为参考。

如果你不确定某个改动是否合理，先回答三个问题：

1. 这个改动是否属于 v1 范围？
2. ABI 是否需要更新？
3. 有没有对应测试能证明它正确？

如果任意一个答案不清楚，先更新文档或补测试，不要直接改 RTL。

## 17. GitHub Actions CI

云端 CI 配置文件在：

```text
.github/workflows/cmake-single-platform.yml
```

它基于 GitHub 官方的 CMake single-platform starter workflow，但已经按
`HolonNPU` 的真实工具链和 release gate 做了项目化配置。

### 17.1 CI 什么时候运行

CI 会在这些情况下运行：

- push 到 `master`。
- 向 `master` 发 pull request。
- push `v*` tag，例如 `v1`。
- 在 GitHub Actions 页面手动触发 `workflow_dispatch`。

### 17.2 CI 使用什么环境

当前 CI 使用：

- `ubuntu-26.04`
- `gcc-15` / `g++-15`
- Ninja
- Verilator
- Python 3
- CMake 4.x

这里特意不用 runner 自带的旧 CMake，因为项目 `CMakePresets.json` 明确要求
CMake 4.0+。CI 也显式使用 `g++-15`，避免 C++26 feature 检测因为默认编译器
过旧而失败。

### 17.3 CI 实际跑哪些命令

云端流程和本地 release gate 对齐：

```sh
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug -j 2 --output-on-failure
ctest --preset lint -j 2 --output-on-failure
cmake --preset regression
cmake --build --preset regression --parallel 2
ctest --preset regression -j 2 --output-on-failure
```

这意味着 CI 不只是“能编译”，还会跑：

- debug 快速测试。
- ABI consistency check。
- RTL interface usage check。
- RTL lint。
- RelWithDebInfo 完整 regression。
- 顶层 `npu_top` 集成测试。

### 17.4 如果 CI 失败，先看哪里

建议按这个顺序定位：

1. 看失败 step 名称：
   - `Configure` 通常是 CMake、编译器、Verilator 包问题。
   - `Build` 通常是 C++/RTL 编译问题。
   - `Test` 或 `Regression test` 通常是 CTest 行为失败。
   - `RTL lint` 通常是 Verilator warning/error。
2. 看失败日志里第一个明确的 test name 或 Verilator diagnostic。
3. 在本地跑同一条命令复现。
4. 如果是 CTest 失败，优先看 `--output-on-failure` 打印的 testbench 信息。
5. 如果需要完整 CTest 日志，CI 失败时会上传 `ctest-logs` artifact。

### 17.5 本地如何复现 CI

本地复现最小命令：

```sh
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug -j 2 --output-on-failure
ctest --preset lint -j 2 --output-on-failure
cmake --preset regression
cmake --build --preset regression --parallel 2
ctest --preset regression -j 2 --output-on-failure
```

如果这些本地通过但 CI 失败，优先检查工具版本：

```sh
cmake --version
verilator --version
c++ --version
python3 --version
```

## 18. 快速命令速查

配置：

```sh
cmake --preset debug
```

构建：

```sh
cmake --build --preset debug --parallel 2
```

快速测试：

```sh
ctest --preset debug -j 2 --output-on-failure
```

RTL lint：

```sh
ctest --preset lint -j 2 --output-on-failure
```

完整 regression：

```sh
cmake --preset regression
cmake --build --preset regression --parallel 2
ctest --preset regression -j 2 --output-on-failure
```

ABI 检查：

```sh
python3 tools/check_abi_consistency.py
```

顶层测试：

```sh
cmake --build --preset debug --target npu_top_tb
ctest --preset regression -R npu_top --verbose
```

GEMM 测试：

```sh
cmake --build --preset debug --target npu_gemm_tb
ctest --preset regression -R npu_gemm --verbose
```

DMA 测试：

```sh
cmake --build --preset debug --target npu_read_dma_tb npu_write_dma_tb
ctest --preset debug -R 'npu_read_dma|npu_write_dma' --verbose
```

driver 测试：

```sh
cmake --build --preset debug --target holon_npu_driver_test
ctest --preset debug -R holon_npu_driver --verbose
```

## 19. 读完本文后你应该能做到什么

读完并按顺序浏览代码后，你应该能够：

- 解释 CPU 如何启动一次 NPU GEMM。
- 找到 AXI-Lite register 实现位置。
- 找到 descriptor 解码位置。
- 找到 DMA read/write 实现位置。
- 找到 systolic array 和 PE 实现位置。
- 找到 GEMM tile scheduler 实现位置。
- 知道 `npu_top.sv` 如何把各模块连接起来。
- 知道 C driver 如何构造和提交 descriptor。
- 知道每个模块对应哪个 testbench。
- 能安全地做小范围修改并运行对应测试。

下一步建议：从 `sim/top_tb.cpp` 开始，跟着一个 `1x1x1` GEMM case，把
descriptor、AXI-Lite 写寄存器、AXI4 memory model、GEMM 结果比对这条链路完整
走一遍。理解这条链路后，整个项目的结构就会清晰很多。
