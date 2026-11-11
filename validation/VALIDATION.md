> 本文保留 15A 初次功能修复的验证说明。随后已在用户批准的 v3 Phase 1 中原位规范化 64 个 User 文件，运行 HEX 不变；最新结果与告警行号见 Phase1_验收报告.md 和 告警逐条映射.md。原有 4 项功能源码差异描述不包含后续排版差异。

# 15A 离线验证记录

## 2026-11-11 欠压恢复

本次镜像以 CoE 20C8:00 = `0x20261111` 标识。当前编译、回归及镜像校验结果见 `build.log`、`run_result.json` 和 `release_check.json`。

- `verify_drive_boot.py` 从低速运行注入母线欠压，检查 PWM 关闭、状态字进入 Fault、故障码为 0x3220。验证过早复位和旧使能均被拦截。重新发出复位沿后，检查红灯熄灭、校准允许短时漏帧，以及发送新使能和速度命令后恢复 PWM 和相位推进。
- `examples/TwinCAT_CSP_SmoothRepeat/verify_example.js` 回放 CSP 主站状态，覆盖故障复位后先发禁用控制字、重新使能和运行，以及通信中断时继续锁存停机。

以上为离线回放。现场复测按 [烧录与验证.md](../烧录与验证.md) 执行。

## 2026-11-03 SysTick 修复记录

本次没有连接下载器、烧录、激活 TwinCAT 或运行实际电机。实机验证待按“烧录与验证.md”完成。

## 编译和发布一致性

ARMCC 5.06 update 6 重编译全部 55 个 C 单元，ARMASM 重建启动向量对象，链接原厂商算法库，生成 AXF / HEX。链接结果 Code=86384、RO-data=5888、RW-data=6280、ZI-data=13296 字节。零错误；11 条编译警告：EEPROM console 的 5 条原有隐式 uart 声明、SaveFunc 的 5 条原有未使用变量，以及延时库为 OS 分支保留的 fac_ms 在非 OS 分支赋值后未使用。详见 build.log。没有新增 UART / 参数 / 电流算法修改。

HEX 全字节与 ELF 的加载内容一致；应用从 0x08000000 起，参数区 0x0803F000..0x0803FFFF 不在镜像内；根目录和 Keil 输出的 AXF / HEX 一致。ESI 的身份和所有 PDO 与 15 的基线一致。实际地址范围和校验和见 release_check.json；包内全文件校验和见 MANIFEST_SHA256.json。

## 针对本次故障的新增集成回放

verify_systick_watchdog.py 使用最终链接的实际 ARM 机器码与向量表，外设寄存器和时间推进由 Unicorn 模拟：

- 原 15 的实际 systick_config() 执行后 CTRL=4、LOAD=0，ENABLE / TICKINT 均未打开，复现漏初始化。
- 15A 在 144MHz 时 LOAD=143999、CTRL 低三位为 7；SysTick 使用最低优先级，实际向量对应 SysTick_Handler，调用实际中断入口会增加 systickTicks。
- delay_us / delay_ms / delay_xms / delay_1ms 的零值、长延时分段、DWT 回绕用例不写 SysTick 的 CTRL / LOAD / VAL，延时结束后真实中断入口仍正常。
- 连续 1000 次真实 SysTick 入口、MotorTick 和主循环 Update 组合可正常喂狗；分别缺失 SysTick、motor 或前台进展时停止喂狗。上电重新使能闭锁保持。
- 四个真实致命异常向量均调用已有输出关闭函数，清 TIM8 MOE 和四路比较值。

4 个新增集成组全部通过。模拟调用并非实机自动中断或真实 1 秒运行时间的证明。

## 原功能回归

14 个运行检查全部返回 0，记录见 run_result.json / run_*.log。其中原有显式计数用例 225 个：参数保护 19、方向限位 27、上电校准 6、PLC 保护桥接 4、电流 25、故障 32、HM 48、PDI 故障 37、HOME PLC 桥接 10、PLC ST 17。另通过完整脉冲 / PP / CSP / CSV 回放；详细场景见 run_verify_pulse_position.log。新增心跳集成 4 组单列。

PLC 六个源工程文件与 15 字节一致，本次未重新运行 XAE 编译。实际电机 PI / 平滑 / PDI 源码及厂商库也与 15 一致。源代码改动仅 systick.c、systick.h、n32g45x_it.c 与保护版本标识 DriveSafety.h；比对结果见 source_scope.json。

## 现场仍需完成

下载并冷启动后，20C8:00=0x20261103；禁用状态 PREOP 至少保持 10 秒，随后 OP 至少保持 30 秒，确认无新的跳 INIT / 0x1C12 超时。再低速空载核对平稳及重新使能行为。DWT 时间精度、真实中断耗时、电流 / 电源衰减及机械动作不能由这些回放代替。
