# 设备树

EDK2 使用前级固件传入的 DTB，经 FDT HOB 提供给驱动。
Meles 和 LPi4A 的板级连接取自同一份 Linux 设备树，不另建
EDK2 专用属性。TH1520 的寄存器布局和初始化顺序由公共驱动实现。

`Th1520BoardSupportLib` 目前解析：

- `status`、`compatible`、`reg` 和供应者的 phandle。
- 默认 pinctrl 状态中的引脚、功能、驱动强度、上下拉和输入配置。
- DW GPIO 的 `gpio-ranges`，以及 PCA9557 的 I2C 地址和引脚。
- `regulator-fixed` 的 GPIO、极性、启动延时和 `vin-supply`。
- `pwm-fan` 的控制器、通道、周期、极性和可选电源。
- USB PHY、hub 和连接器的电源引用。

供电先设置 GPIO 输出值和方向，再连接引脚复用；共享电源按
phandle 去重。USB 切换角色只改变路由，不重新开关 hub 或 VBUS。
固定电源的极性按绑定中的 `enable-active-high` 解释。
风扇在启动时使用全速，周期按继承的 PWM 时钟计算，不重设 PLL。
GMAC 复用公共 pinctrl 解析，并保留原有控制器和共享 MDIO 的发现。

这不是完整的 Linux 设备模型：目前仅支持 TH1520 的直接地址映射、
默认状态位于 `pinctrl-0` 的相关引脚和固定电源。未知控制器、引脚
或格式报错；供电计划和 pinctrl 状态在写寄存器前完成检查。
早期资源失败会记录串口错误，仍继续启动以便恢复。

USB 主机／设备路由仍在板级库中。静态 `gpio-hog` 没有描述两种
角色各自需要的电平，不能据此推导 Fastboot 的动态切换规则。
F2/F9/F12、默认启动项等启动策略也不属于设备树。

用 `make edk2-meles-usb-check edk2-lpi4a-usb-check edk2-gmac-check`
对两块板的实际 DTB 运行主机测试；DTB 默认取自 `../build`。
测试覆盖供电顺序、共享 hub 电源、角色切换、PWM 参数和错误引用。

2026-09-22 在 Meles 16 GiB 上刷写并验证：PWM2 周期、占空比均为
1,250,000，继承时钟为 125 MHz，对应 DT 的 10 ms 全速输出。
两个 GL3523 peer hub 和 U 盘正常枚举；eMMC 约 41.7 MiB/s、USB
约 38.3 MiB/s，两遍 64 MiB 读取校验一致。GMAC SNP 回环校验
16,384 帧通过，Linux 接管及正常重启通过。LPi4A 完成构建和实际
DTB 的主机测试，本轮未在 LPi4A 上刷写验证。
