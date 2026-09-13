# CH582M 电容触摸 PCB 键盘

基于 [RicardoX2X 的名片键盘项目](https://hackaday.io/project/197940-capacitive-touch-keyboard-business-card)，按用户提供的 `SCH_Schematic_Keyboard_1-P1_2026-09-11.svg` 适配 CH582M。Type-C 的 D+/D− 接 PB11/PB10，固件作为 USB HID Boot Keyboard 向电脑发送输入，无须专用键盘驱动。

当前完成代码编译及主机端逻辑测试，**尚未进行实板烧录、USB 枚举和触摸灵敏度测试**。原作者仓库在本次查阅时的 `3a5249a` 提交只有串口示例，并非可直接使用的键盘固件。

## 引脚和键位

| 网络 | CH582M 引脚 | TouchKey/ADC 通道 |
|---|---|---|
| COLUMN1～7 | PA4、PA5、PA12、PA13、PA14、PA15、PA3 | 0、1、2、3、4、5、6 |
| ROW1～7 | PA6、PA0、PA1、PA2、PA7、PA8、PA9 | 10、9、8、7、11、12、13 |
| ROW8_NTOUCH | PB8 | GPIO 放电时间检测 |
| ROW8_AUX | PB9 | 通过 R7（1 MΩ）连接 PB8 节点 |
| USB D+ / D− | PB11 / PB10 | USB 控制器 1 |

按用户确认的实物布局，表中列号采用原理图网络名。右半区从左到右是 C7→C3。

| 行 | C1 | C2 | C3 | C4 | C5 | C6 | C7 |
|---|---|---|---|---|---|---|---|
| R1 | Esc | Q | W | E | R | T | Y |
| R2 | Tab | A | S | D | F | G | H |
| R3 | Shift | Z | X | C | V | B | N |
| R4 | Ctrl | Win | Alt | Fn | Space | Space | Space |
| R5 | — | — | Backspace | P | O | I | U |
| R6 | — | — | Enter | ; | L | K | J |
| R7 | — | — | / | ↑ | . | , | M |
| R8 | — | — | → | ↓ | ← | Fn | Space |

两个 Fn 均支持 Q/W/E/R/T/Y/U/I/O/P → 1/2/3/4/5/6/7/8/9/0。多个空格电极合并为一个 Space Usage。状态统一使用 `8×7` 坐标，48 个有效交叉点，不再混用压缩索引。

## 检测方法与边界

这不是带独立开关的机械键盘矩阵。当前实现分别读取 7 个列电极、7 个行电极以及 R8，按行列触摸状态解析交叉点。GPIO 保持浮空输入，TouchKey 使用内置充放电采样。

R8 的 PB8 节点同时经 R7 接 PB9、经 R8（1 MΩ）接地。经 R7 充电的稳态电压只有约 VDD/2，不能可靠假设会达到数字高电平门限。因此 PB9 保持低，PB8 短暂推挽充电后切回浮空输入，测量放电至低电平的时间；与无触摸基准比较。此过程仍须在实板上验证可分辨的时间差。

上电稳定约 300 ms 后采集 32 轮基准；插线后的第一秒不要触摸键盘。检测使用迟滞、连续 3 帧确认和空闲基准缓慢跟踪。每约 5 ms 尝试扫描一次，实际周期取决于采样耗时。转换和 R8 检测均有超时，故障释放所有按键；初次校准失败时仍维持 USB 服务，并每秒重试。

**独立行列自电容测量不能唯一识别任意多键组合。** 单行或单列组合可直接识别；多行且多列同时触摸时，保留仍有行列支持的已按下键，不生成所有笛卡尔积交叉点。在已按住一个键后，如果仅多出一行一列，则推断为新增键，可用于先按住 Shift/Ctrl/Fn 再按字母。这个推断依赖按键先后顺序；同时按下、滑动或替换触点可能仍有歧义。当前固件不保证任意多键无冲，HID 的 6 键报告容量不代表传感器能识别 6 个任意触点。

## 构建

本机已经安装 WCH RISC-V Embedded GCC，可在项目目录运行：

```powershell
.\build.ps1
.\test.ps1
```

`build.ps1` 从源码重新编译，输出 `build/CH582M.hex`、`.bin`、`.elf`、`.map`，不依赖旧的 `obj` 目录或旧工程路径。其他电脑可指定编译器目录：

```powershell
.\build.ps1 -ToolchainBin 'C:\你的安装位置\RISC-V Embedded GCC\bin'
```

`test.ps1` 使用主机 C 编译器（本机为 `C:\MinGW\bin\gcc.exe`）测试 48 个交叉点、右半区布局、Fn、修饰键、空格去重、歧义抑制、松键，以及模拟 USB 寄存器下的枚举、控制请求、端点忙、空闲重发、暂停和复位。模拟测试不代替真实 USB 总线测试。

MounRiver 可继续打开 `CH582M.wvproj` 并重新构建，产物在 `obj/`。已将 ADC 驱动加入编译，并排除主机测试与 `build/`；如 IDE 已打开，请重新加载工程以更新文件列表。`.mrs/launch.json` 已改为当前项目路径，调试文件仍使用 IDE 的 `obj/CH582M.elf`。脚本生成的固件则使用 `build/CH582M.elf`。

启动文件和库名中的 `CH583` / `ISP583` 来自 CH58x 共用 SDK，不表示把目标改成 CH583。工程目标仍为 CH582M，链接区域为 448 KiB Flash、32 KiB RAM。

## 烧录与实板验证

1. 用 WCH-Link/MounRiver 下载新生成的 `build/CH582M.hex`，或下载 IDE 重新构建的 `obj/CH582M.hex`。本次修改未执行烧录。
2. 断开再接 Type-C 数据线，第一秒不要碰电极。在电脑设备管理器/系统 USB 列表中确认出现 HID 键盘。
3. 在空白文本编辑器中逐个测试字母、Backspace、Enter、方向键，再测试松开是否停止输入，最后测试先按 Fn 后按字母。
4. 若没有触摸响应或误触发，通过调试器读取 `touch_ready`、`touch_faults`、`touch_raw[]`、`touch_base[]`、`touch_delta[]`。数组 0～6 对应 C1～C7，7～13 对应 R1～R7，14 对应 R8。
5. 在 `src/Main.c` 调整 `PRESS_DELTA=80`、`RELEASE_DELTA=45`（ADC 计数）及 `R8_PRESS_US=8`、`R8_RELEASE_US=4`（微秒）。这些是起始值，必须根据实板空闲噪声和手指触摸差值调节。R8 触摸预期放电时间增加；其他通道使用相对基准的差值绝对值。

USB 使用单接口、单 EP1 IN、8 字节 Boot Keyboard 报告，处理 EP0 枚举、HID 协议/空闲/LED 请求、端点 halt、复位和暂停。发送缓冲区在电脑确认接收前不会被覆盖，松键报告会在端点空闲后发送。沿用原固件 VID/PID `1A86:5524` 用于开发，未申请产品专用 VID/PID。

## 参考

- [原项目与设计背景](https://hackaday.io/project/197940-capacitive-touch-keyboard-business-card)
- [原作者固件仓库](https://github.com/RicardoX2X/Business_card)
- [WCH CH582/CH583 官方 SDK](https://github.com/openwch/ch583)
- [WCH USB CompoundDev 示例](https://github.com/openwch/ch583/blob/main/EVT/EXAM/USB/Device/CompoundDev/src/Main.c)：USB 寄存器操作参考。
