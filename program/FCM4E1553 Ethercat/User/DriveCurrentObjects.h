/**
 * @file    DriveCurrentObjects.h
 * @brief   电流控制适配对象字典片段
 * @details 根据运行与保持电流配置衔接原厂电流计算及PWM输出。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    按工程约定不添加头文件保护，保持现有SSC集成方式。
 */
/* Included once with _CiA402_. Configuration only; never RxPDO-mappable. */
#include "DriveCurrent.h"
UINT8 WriteDriveCurrent(UINT16, UINT8, UINT32, UINT16 MBXMEM *, UINT8);
OBJCONST TSDOINFOENTRYDESC OBJMEM dcDesc20F1 =
    {DEFTYPE_INTEGER16, 16, ACCESS_READ | ACCESS_WRITE_PREOP};
OBJCONST TSDOINFOENTRYDESC OBJMEM dcDescUnsigned =
    {DEFTYPE_UNSIGNED16, 16, ACCESS_READ | ACCESS_WRITE_PREOP};
OBJCONST UCHAR OBJMEM dcName20F1[] = "Run phase peak current mA (0=off; 100..3000)";
OBJCONST UCHAR OBJMEM dcName20D5[] = "Hold current percent of run (10..100)";
OBJCONST UCHAR OBJMEM dcName20F5[] = "Hold current delay ms (1..30000)";
#define DRIVE_CURRENT_OD_ENTRIES \
    {NULL,NULL,0x20D5,{DEFTYPE_UNSIGNED16,OBJCODE_VAR << 8},&dcDescUnsigned,dcName20D5,&gDriveCurrentConfig.holdPercent,NULL,WriteDriveCurrent,0}, \
    {NULL,NULL,0x20F1,{DEFTYPE_INTEGER16,OBJCODE_VAR << 8},&dcDesc20F1,dcName20F1,&gDriveCurrentConfig.runPeakMa,NULL,WriteDriveCurrent,0}, \
    {NULL,NULL,0x20F5,{DEFTYPE_UNSIGNED16,OBJCODE_VAR << 8},&dcDescUnsigned,dcName20F5,&gDriveCurrentConfig.holdDelayMs,NULL,WriteDriveCurrent,0}
