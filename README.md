# TH1520 EDK2

本仓库只维护 TH1520 的 EDK2 平台代码，支持 Milk-V Meles 和
Sipeed Lichee Pi 4A。

- `platforms/Th1520Pkg/`：共用驱动、库、Fastboot 和启动界面。
- `platforms/MelesPkg/`：Meles 板级配置。
- `platforms/Lpi4aPkg/`：LPi4A 板级配置。
- `tests/`：EDK2 驱动的主机测试及 HSM、I/O EFI 测试应用。
- `tools/build.sh`、`Makefile`：EDK2 构建和测试入口。
- `docs/display.md`：HDMI 实现及已有硬件验证记录。
- `docs/device-tree.md`：固件解析设备树的范围和板级边界。
- `docs/recovery.md`：RAM 恢复配置及原始固件刷写目标。
- `docs/io-performance.md`：SPI、eMMC、USB 和网卡测速及复现方式。

U-Boot、OpenSBI、Linux 设备树、DDR/AON、镜像打包和刷写流程
继续由上层 `th1520` 仓库维护，本次不迁移。

## 构建

默认使用现有的 `../edk2` 源码目录及其已初始化的子模块。
验证使用的上游版本为 `6abc1c06585ffd66d23a74811a930d8d0f88cef7`。
需要 RISC-V GCC 工具链、GCC/G++、make、Python 3、NASM、
UUID 和 OpenSSL 开发包；主机测试还需要 libfdt 开发包。

```sh
make edk2 EDK2_BOARD=meles
make edk2 EDK2_BOARD=lpi4a
# 最小串口 Fastboot 恢复配置：
make edk2-recovery
# 上游源码在其他目录时：
make edk2 EDK2_SOURCE=/path/to/edk2 EDK2_BOARD=meles
```

默认 `EDK2_TARGET=RELEASE`、`JOBS=8`，工具链前缀为
`riscv64-linux-gnu-`，可通过 `CROSS_COMPILE` 覆盖。
构建工作目录为本仓库的 `build/`，不修改上游 EDK2 源码。
产物分别为 `build/meles/TH1520.fd` 和 `build/lpi4a/TH1520.fd`。
这些是 EDK2 固件卷，不能直接刷成完整 SPI 或 boot0 镜像。

完整镜像的构建和升级说明暂留在上层仓库：
[Meles](../docs/edk2.md)、[LPi4A](../docs/edk2-lpi4a.md)。
上层现有构建入口仍使用其原有平台副本；本次只提取 EDK2，
后续再统一上层的构建引用。

## 测试

```sh
make check
make edk2-hsm-test EDK2_BOARD=lpi4a
```

`make check` 检查显示、DMA、复位、MAC 地址和存储驱动。
HSM 测试产物在
`build/Build/Lpi4a/RELEASE_GCC/RISCV64/HsmTest.efi`，
需在目标板 UEFI Shell 运行，不包含在默认固件中。

需要验证 Fastboot 镜像校验、设备树解析和 USB 供电时，使用已构建的
两块板的镜像与 DTB；这些检查不会构建或修改外部固件：

```sh
make check-artifacts EDK2_BOARD=meles
make check-artifacts EDK2_BOARD=lpi4a
# 默认读取 ../build，也可覆盖：
make check-artifacts FIRMWARE_BUILD=/path/to/th1520/build
```

## 来源

平台实现提取自 `th1520` 的 `74380f02774d`，按功能整理提交。
原仓库历史保持不变。保留各文件的 SPDX 许可证、版权及
openRuyi 图案的来源说明。显示文档中的上板记录属于提取前的
验证，本次目录整理不代表重新刷写验证。
