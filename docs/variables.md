# 变量和 MAC 保存

Meles 的 SPI 支持 UEFI 变量持久化。Linux 使用标准 Runtime Services，
不需要修改内核，也没有私有 Linux 后端、握手变量或 SBI 存储扩展。

LPi4A 默认不配置 SPI，普通 UEFI 变量维持内存模拟模式。eMMC boot0
仅保留启动阶段使用的 MAC 记录，不提供 eMMC 的运行时变量写入。

## 存储布局

| 平台 | 固件区上限 | 保留区 |
| --- | --- | --- |
| Meles，8 MiB SPI | `0x7c0000` 字节 | SPI 最后 256 KiB，变量双副本 |
| LPi4A，4 MiB boot0 | `0x3fe000` 字节 | boot0 最后 8 KiB，MAC 双副本 |

SPI 每个副本占 128 KiB，容纳最多 64 KiB 变量数据。先写数据并
校验，最后提交带序号与 CRC 的头部；成功前保留另一个有效副本。
boot0 使用两个相隔 4 KiB 的 512 字节记录，带版本、序号和 CRC。
版本 2 同时容纳默认随机 MAC 和 BIOS 配置，兼容已有版本 1 记录。
更新写入另一个副本；断电或校验失败时继续使用旧副本。
已有默认随机地址保持不变，相同配置不会重复写入。

新版 Fastboot 的 `spi`、`emmcboot0` 及普通固件更新路径都排除
保留区；boot1 不再承担变量存储。现有约 1.4 MiB 的 LPi4A 镜像仍
能放入固件区。外部编程器和旧版整片擦写工具不受这些边界约束。

## MAC 选择

优先级为：合法 DT 地址、合法 BIOS 设置、合法 eFuse 地址、
已保存的默认随机地址、首次生成并保存的随机地址。所有来源都检查合法性，不合法的候选
直接跳过。地址必须是 6 字节、非零、非广播、非组播；不会将非法
地址强行修正后使用。保存的随机基址另需符合本地管理、偶数尾字节
的生成格式，boot0 记录还需通过版本和 CRC 检查。

DT 的 `mac-address` 非法时继续检查 `local-mac-address`，两者都
不可用才继续检查 BIOS 设置和 eFuse。合法的显式地址视为有意配置，
不强制相邻。两个网口的候选先统一收集，避免探测
顺序影响结果。GMAC1 只有进入回退路径时才使用 GMAC0 最终地址的
相邻地址，进位不得产生组播地址。选定结果同时回填运行时 DT 的
`mac-address` 和 `local-mac-address`。

MAC 只由 EDK2 在启动阶段处理。SPI 中的 `FallbackMac` 不带
Runtime Access 属性，并在 DXE 阶段结束后锁定；即便尚未创建，
Linux 也不能通过 Runtime SetVariable 创建或改写这个保留名称。
boot0 存储协议仅存在于 Boot Services，结束 DXE 后锁定默认随机
记录；BIOS 配置仍可通过启动阶段的设置页面更新。
SPI 中的 `MacSettings` 也不带 Runtime Access 属性，运行时服务
明确拒绝创建、修改或删除这两个保留名称。
Linux 只接收最终 MAC，没有用于修改持久 MAC 的专用接口。

## BIOS 设置

启动等待时间在 `Boot Maintenance Manager → Auto Boot Time-out` 设置。
用 F10 确认保存后，正常固件读取标准 `Timeout` 变量；仅在变量不存在
时采用 10 秒默认值。RAM 恢复固件仍直接进入 Fastboot，不等待倒计时。

进入 `F2 → Device Manager → Ethernet MAC Addresses`：

- 勾选 `Use BIOS address` 后手动输入六个十六进制字节，用冒号分隔。
- `Generate new random addresses` 生成预览，同时打开 BIOS 覆盖。
  双网口生成相邻地址；如果 GMAC0 由 DT 指定，GMAC1 使用它的相邻值。
- `Restore automatic selection` 清除 BIOS 覆盖，保留原默认随机地址。
- `Save and restart` 保存并重启生效。也可以用标准 F10 保存后再重启。

页面只显示 DT 中存在的网口。DT 指定的地址仍优先，对应编辑项
置灰。全零、组播、广播或格式不正确的输入不会保存，并显示错误。
手动输入和重新生成写入同一份 BIOS 配置，不改写 eFuse。

## 依赖及运行时边界

- 通用变量服务依赖 EDK2 VariableRuntimeDxe，负责标准变量校验；
  Th1520NvStoreDxe 负责介质保存、地址转换和升级保留。
- Meles 需要配套 `Firmware.dts`。它补充 QSPI0 控制器、时钟与
  原生片选引脚，NOR 子节点设为 `reserved`，不交给 SPI NOR/MTD。
- Linux 使用已有的 DesignWare SPI 控制器、TH1520 时钟和 pinctrl
  驱动维持控制器资源；不需要新增或修改 Linux 驱动。
- SPI 运行时使用原生片选，仅操作控制器寄存器，不改共享 GPIO、
  pinctrl 或时钟门控。控制器未就绪时返回错误，不假报保存成功。
- LPi4A 的 MAC 依赖 EDK2 eMMC 驱动；GMAC 等待存储协议就绪后再
  选择地址。交接 Linux 后不再访问 MMC 控制器。
- OpenSBI 只保留既有标准服务，没有为变量或 MAC 增加接口。

```sh
make edk2-dtb EDK2_BOARD=meles
make edk2-dtb EDK2_BOARD=lpi4a
```

DT 的基础板级描述来自 `KERNEL_SOURCE`，默认 `../linux-mainline`。
输出是 `build/<board>/firmware.dtb`，需与 FD 一同交给镜像打包工具。
Meles 不能只更新 FD 而继续使用缺少上述资源描述的旧 DTB。

### C910 运行时 MMIO

当前 RISC-V Linux EFI 映射把 MMIO 作为普通内存。C910 的 XTheadMae
使用 MAEE 页表属性，不能套用标准 Svpbmt 的 PBMT 编码。固件预留
页表，仅映射一页寄存器访问代码及 QSPI、AP 寄存器页；设备页采用
T-Head SO/SH 属性，不设置普通内存的 C/B 位。

每次运行时寄存器访问暂时屏蔽本核中断、切换到这份独立页表，完成
单次 32 位访问后恢复原 `satp` 和中断状态。切换区间只使用寄存器，
不访问调用者栈，也不修改 Linux 页表或请求其他核配合。页表转换
失败时拒绝持久化写入。这个兼容路径只用于 TH1520 固件内部。

## 验证范围

Meles 已验证标准 efivarfs 写入、重启和固件升级后的变量保留。
默认随机 MAC 保持为 `C2:F5:52:1A:87:16`；广播和组播 DT 输入均被
忽略，最终地址回填到两个 DT MAC 属性。两个私有 MAC 变量对 Linux
不可见，Runtime SetVariable 写入返回写保护。

BIOS 页面已验证手动设置、重新生成、重启生效和恢复原默认随机
地址。LPi4A 已完成构建和 boot0 记录迁移、更新中断等主机测试；
本轮未接 LPi4A，未进行该板真机验证。HDMI 插拔按用户要求未测试。

2026-09-23 在 Meles 补测了启动项注册，使用 ESP 上已有的
`\EFI\ubuntu\grubriscv64.efi`，没有改写引导文件：

- Linux `efibootmgr` 新增启动项并置于 `BootOrder` 首位；重启后
  `BootCurrent` 确认选中新增项，条目和顺序均保留。
- 恢复默认优先项后设置 `BootNext`；重启选中指定项，随后清除
  `BootNext`，默认顺序保持不变。
- BIOS `Add Boot Option` 新增的完整设备路径，在重启后的 F12
  菜单中保留并成功启动；`BootCurrent` 同样确认选中了新增项。
- 删除测试项并恢复原顺序后再次重启，确认回到原默认启动项，
  启动项集合与测试前相同，没有残留测试项或重复固件菜单项。
