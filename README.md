> 2026-11-03：本工程已原位完成 v3 Phase 1 排版、CP936 编码和文件头补齐。运行 HEX 与规范化前逐字节一致，include / guard / 宏均保留，0 error、原有 11 条告警逐条映射。本轮 14 项验证脚本全部通过（含 48 项 HM）。见 validation/Phase1_验收报告.md；下文功能说明保持原意。

# 15A：SysTick / 看门狗及欠压恢复修复

2026-11-11 欠压修复版可通过 CoE 20C8:00 的值 `0x20261111` 确认。电机保护切断 PWM 后，CiA402 直接报告 Fault，24 V 母线欠压对应 603F:00 = `0x3220`。故障复位后的无输出零点校准只计入新采样，允许短时漏帧。随附的 TwinCAT 回零示例在接受新的使能沿时先发送一周期禁用控制字。现场复测步骤见 [烧录与验证.md](烧录与验证.md)。

当前恢复目标是第一台 FCM4E1553 / N32G455 的稳定通信。本目录为单独工程，应用直接从 0x08000000 启动，不含 FoE Bootloader。原 15、16、17 文件没有覆盖；当前恢复请使用本目录。

## 已确认的问题与修改

原 15 的看门狗要求主循环、TIM8 电机中断、SysTick 三条路径均有进展才喂狗。但非 OS 延时库的 systick_config() 只设置时钟源，并未开启周期性 SysTick 中断；delay_us()/delay_ms() 又临时占用并关闭 SysTick。因此 SysTick 心跳不能持续产生，触发名义约 400ms 的 IWDG 复位。现场约 0.44s 一次退回 INIT 的周期与此相符。0x1C12 下载超时可由反复复位造成，应先恢复稳定通信。

修复：

- SysTick 以 HCLK 配置为 1ms 周期中断，最低中断优先级；实际 SysTick_Handler 发布看门狗心跳。
- 非 OS 延时改用 DWT 自由运行周期计数器，处理计数回绕及长延时分段，不再开关 SysTick。
- 四个实际异常入口 HardFault / MemManage / BusFault / UsageFault 接入已有的 PWM 输出关闭函数，再等待看门狗复位。
- 保护版本 20C8:00 改为 0x20261103。

看门狗保持名义约 400ms、三路径共同进展才喂狗的要求。电机算法、原两级平滑、电流 PI、EtherCAT PDI、PDO、SII 身份和参数记录格式均未修改。上电仍需重新使能。源码变化及 PLC 逐文件一致性记录见 validation/source_scope.json。

## 使用文件

- Keil 工程：program/FCM4E1553 Ethercat/MDK-ARM/Project.uvprojx，目标 15A_Protection_SysTickFix。
- 可烧录 HEX：FCM_Protection_SysTickFix_20261103.hex。
- 调试 AXF：FCM_Protection_SysTickFix_20261103.axf。工程内 N32G455/N32G455.axf 与之完全相同。
- 操作：烧录与验证.md。
- 验证：validation/VALIDATION.md，validation/run_result.json 及各日志。
- Git 仓库：https://github.com/phchonloo/ether-cat-fixbug。每轮修改完成并验证后提交并推送一次。

现场可沿用已有 SII、ESI 和 PDO 配置。若使用随附的 TwinCAT 回零或 CSP 循环运行示例，请更新对应的 MAIN。其他主站程序可按烧录说明发送控制字。

## 保留的参数与功能

24V 母线及逻辑电源欠压保护、普通模式方向限位、HOME X1 的 HM 方法 17～22 / 37、参数双页保存及上电重新使能逻辑均沿用 15。20F1 和 6083 / 6084 / 6085 出厂默认仍为 3000。2000=0 时加速度单位 rpm/s；2000=1 时按每转计数转换。609A 为 count/s²，默认 50000（1000 count/rev 时相当于 3000 rpm/s）。

参数 Flash 为 0x0803F000～0x0803FFFF；本次镜像不覆盖参数页，参数 schema 不变。仅已通过 1010:01 保存的设置才会恢复；从未保存时使用出厂默认。复位不恢复使能、目标或回零完成状态。

## 离线复现

本机 ARMCC 5.06u6：先运行 tools/build_firmware.py，再运行 tools/run_validation.py。后者只编译 / 仿真 / 检查文件，不连接硬件。Python 仿真依赖见 tests/requirements.txt，安装至 tests/.pulse_test_deps；依赖缓存已加入 .gitignore。完整工程可以由 Keil 重新构建。

离线测试无法证明实机通信、DWT 时基精度、电流 / ISR 用时及机械动作。实板烧录后的 PREOP 保持与运行验证仍需按步骤完成。
