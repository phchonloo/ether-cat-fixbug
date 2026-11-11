# Phase 1 include 依赖对照

所有源文件的直接 include 内容与顺序均保持，新增 0、删除 0；预处理指令和 guard 不变。每个头文件的完整对照见 include依赖对照.json；保守的间接依赖图见“预处理依赖.json”，该图包含条件分支，用于多编译受影响单元，不改变宏定义或项目引用。

| .c 文件 | 修改前直接包含 | 修改后直接包含 | 新增 / 删除 |
|---|---|---|---|
| [AdcInit.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/AdcInit.c>) | `"AdcInit.h"`；`"DriveTelemetry.h"` | 与左列逐项同序 | 0 / 0 |
| [DriveBoot.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/DriveBoot.c>) | `"DriveBoot.h"`；`"DriveCurrent.h"`；`"DriveFault.h"`；`"EthercatMotorControl.h"`；`"Errorhadle.h"`；`"cia402appl.h"`；`"qspi.h"` | 与左列逐项同序 | 0 / 0 |
| [DriveCurrent.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/DriveCurrent.c>) | `"DriveCurrent.h"`；`"DriveTelemetry.h"`；`"DriveBoot.h"`；`"DriveParameters.h"`；`"EthercatMotorControl.h"`；`"cia402appl.h"`；`"coeappl.h"`；`"MotionPID.h"` | 与左列逐项同序 | 0 / 0 |
| [DriveFault.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/DriveFault.c>) | `"DriveFault.h"`；`"DriveTelemetry.h"`；`"DriveCurrent.h"`；`"DriveBoot.h"`；`"DriveSafety.h"`；`"DriveParameters.h"`；`"EthercatMotorControl.h"`；`"Errorhadle.h"`；`"cia402appl.h"`；`"coeappl.h"` | 与左列逐项同序 | 0 / 0 |
| [DriveParameters.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/DriveParameters.c>) | `"DriveParameters.h"`；`"DriveSafety.h"`；`"DriveCurrent.h"`；`"DriveFault.h"`；`"DriveBoot.h"`；`"DriveHoming.h"`；`"EthercatMotorControl.h"`；`"Errorhadle.h"`；`"cia402appl.h"`；`"coeappl.h"`；`"n32g45x_flash.h"`；`"qspi.h"`；`<stddef.h>` | 与左列逐项同序 | 0 / 0 |
| [DriveSafety.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/DriveSafety.c>) | `"DriveSafety.h"`；`"EthercatMotorControl.h"`；`"DriveFault.h"`；`"DriveParameters.h"`；`"Errorhadle.h"`；`"IoIntInit.h"`；`"cia402appl.h"`；`"qspi.h"`；`"n32g45x_pwr.h"`；`"n32g45x_iwdg.h"`；`"n32g45x_exti.h"` | 与左列逐项同序 | 0 / 0 |
| [DriveTelemetry.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/DriveTelemetry.c>) | `"DriveTelemetry.h"`；`"DriveParameters.h"`；`"DriveFault.h"`；`"DriveBoot.h"`；`"AdcInit.h"`；`"EthercatMotorControl.h"`；`"cia402appl.h"`；`"coeappl.h"` | 与左列逐项同序 | 0 / 0 |
| [Errorhadle.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/Errorhadle.c>) | `"Errorhadle.h"`；`"DriveFault.h"`；`"DriveSafety.h"` | 与左列逐项同序 | 0 / 0 |
| [EthercatMotorControl.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/EthercatMotorControl.c>) | `"EthercatMotorControl.h"`；`"DriveFault.h"`；`"DriveCurrent.h"`；`"DriveBoot.h"`；`"DriveSafety.h"`；`"DriveParameters.h"`；`"ecat_def.h"`；`"applInterface.h"`；`"cia402appl.h"`；`"Errorhadle.h"`；`"EcatPulsePosition.h"`；`"DriveHoming.h"`；`"IoIntInit.h"`；`"qspi.h"`；`"ecatappl.h"`；`"DriveHomingImpl.h"` | 与左列逐项同序 | 0 / 0 |
| [IoIntInit.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/IoIntInit.c>) | `"IoIntInit.h"` | 与左列逐项同序 | 0 / 0 |
| [PulseHandle.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/PulseHandle.c>) | `"PulseHandle.h"`；`"EthercatMotorControl.h"` | 与左列逐项同序 | 0 / 0 |
| [PwmInit.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/PwmInit.c>) | `"PwmInit.h"`；`"EthercatMotorControl.h"`；`"DriveCurrent.h"`；`"DriveFault.h"`；`"DriveBoot.h"`；`"DriveSafety.h"`；`"qspi.h"` | 与左列逐项同序 | 0 / 0 |
| [SaveFunc.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/SaveFunc.c>) | `"SaveFunc.h"` | 与左列逐项同序 | 0 / 0 |
| [SciInit.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/SciInit.c>) | `"SciInit.h"` | 与左列逐项同序 | 0 / 0 |
| [can.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/can.c>) | `"can.h"` | 与左列逐项同序 | 0 / 0 |
| [eeprom.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/eeprom.c>) | `"eeprom.h"` | 与左列逐项同序 | 0 / 0 |
| [eeprom_console.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/eeprom_console.c>) | `"mcuinit.h"` | 与左列逐项同序 | 0 / 0 |
| [exti.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/exti.c>) | `"exti.h"` | 与左列逐项同序 | 0 / 0 |
| [flash.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/flash.c>) | `"Flash.h"` | 与左列逐项同序 | 0 / 0 |
| [main.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/main.c>) | `"mcuinit.h"`；`"EthercatMotorControl.h"`；`"SciInit.h"`；`"DriveTelemetry.h"`；`"DriveCurrent.h"`；`"DriveFault.h"`；`"DriveBoot.h"`；`"DriveSafety.h"`；`"DriveParameters.h"` | 与左列逐项同序 | 0 / 0 |
| [n32g45x_it.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/n32g45x_it.c>) | `"n32g45x_it.h"`；`"systick.h"`；`"DriveSafety.h"` | 与左列逐项同序 | 0 / 0 |
| [qspi.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/qspi.c>) | `"qspi.h"` | 与左列逐项同序 | 0 / 0 |
| [rs485.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/rs485.c>) | `"rs485.h"` | 与左列逐项同序 | 0 / 0 |
| [timer.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/timer.c>) | `"mcuinit.h"` | 与左列逐项同序 | 0 / 0 |
| [u_i2c.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/u_i2c.c>) | `"mcuinit.h"` | 与左列逐项同序 | 0 / 0 |
| [uartc.c](<C:/Users/pc/Desktop/最新固件与PLC_20261029/15A_FCM_Protection_SysTickFix_20261103/program/FCM4E1553 Ethercat/User/uartc.c>) | `"mcuinit.h"` | 与左列逐项同序 | 0 / 0 |

qep.c 未纳入；没有为其调整 uvprojx 或启用 TIM2 编码器实现。
