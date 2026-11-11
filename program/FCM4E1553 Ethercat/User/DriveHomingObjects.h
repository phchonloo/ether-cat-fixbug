/**
 * @file    DriveHomingObjects.h
 * @brief   回零配置与诊断对象字典片段
 * @details 声明回零配置、诊断状态及现有对外接口。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    按工程约定不添加头文件保护，保持现有SSC集成方式。
 */
/* Included once by cia402appl.h, no changes to existing PDO layouts. */
#include "DriveHoming.h"
UINT8 ReadDriveHoming(UINT16, UINT8, UINT32, UINT16 MBXMEM *, UINT8);
UINT8 WriteDriveHoming(UINT16, UINT8, UINT32, UINT16 MBXMEM *, UINT8);
OBJCONST TSDOINFOENTRYDESC OBJMEM hmI8 = {DEFTYPE_INTEGER8, 8, ACCESS_READ | ACCESS_WRITE_PREOP};
OBJCONST TSDOINFOENTRYDESC OBJMEM hmI32 = {DEFTYPE_INTEGER32, 32, ACCESS_READ | ACCESS_WRITE_PREOP};
OBJCONST TSDOINFOENTRYDESC OBJMEM hmU32 = {DEFTYPE_UNSIGNED32, 32, ACCESS_READ | ACCESS_WRITE_PREOP};
OBJCONST TSDOINFOENTRYDESC OBJMEM hmRead16 = {DEFTYPE_UNSIGNED16, 16, ACCESS_READ};
OBJCONST TSDOINFOENTRYDESC OBJMEM hmRead32 = {DEFTYPE_UNSIGNED32, 32, ACCESS_READ};
OBJCONST TSDOINFOENTRYDESC OBJMEM hmSpeedsDesc[] = {
    {DEFTYPE_UNSIGNED8, 8, ACCESS_READ},
    {DEFTYPE_UNSIGNED32, 32, ACCESS_READ | ACCESS_WRITE_PREOP},
    {DEFTYPE_UNSIGNED32, 32, ACCESS_READ | ACCESS_WRITE_PREOP}};
OBJCONST UCHAR OBJMEM hmN607C[] = "Home offset counts";
OBJCONST UCHAR OBJMEM hmN6098[] = "Homing method (17..22,37)";
OBJCONST UCHAR OBJMEM hmN6099[] = "Homing speeds count/s\000Switch search\000Switch release\000\377";
OBJCONST UCHAR OBJMEM hmN609A[] = "Homing acceleration count/s^2";
OBJCONST UCHAR OBJMEM hmN60FD[] = "Digital inputs (bit0 neg X2,bit1 pos X3,bit2 HOME X1)";
OBJCONST UCHAR OBJMEM hmN20E0[] = "Homing state";
OBJCONST UCHAR OBJMEM hmN20E1[] = "Homing timeout ms";
OBJCONST UCHAR OBJMEM hmN20E2[] = "Homing maximum excursion counts";
OBJCONST UCHAR OBJMEM hmN20E3[] = "Homing error detail";
OBJCONST UCHAR OBJMEM hmN20E4[] = "Homing implementation version";
#define DRIVE_HOMING_OD_ENTRIES \
 {NULL,NULL,0x607C,{DEFTYPE_INTEGER32,OBJCODE_VAR << 8},&hmI32,hmN607C,&gDriveHomingConfig.offset,ReadDriveHoming,WriteDriveHoming,0}, \
 {NULL,NULL,0x6098,{DEFTYPE_INTEGER8,OBJCODE_VAR << 8},&hmI8,hmN6098,&gDriveHomingConfig.method,ReadDriveHoming,WriteDriveHoming,0}, \
 {NULL,NULL,0x6099,{DEFTYPE_UNSIGNED32,2 | (OBJCODE_REC << 8)},hmSpeedsDesc,hmN6099,&gDriveHomingSpeeds,ReadDriveHoming,WriteDriveHoming,0}, \
 {NULL,NULL,0x609A,{DEFTYPE_UNSIGNED32,OBJCODE_VAR << 8},&hmU32,hmN609A,&gDriveHomingConfig.acceleration,ReadDriveHoming,WriteDriveHoming,0}, \
 {NULL,NULL,0x60FD,{DEFTYPE_UNSIGNED32,OBJCODE_VAR << 8},&hmRead32,hmN60FD,(void *)&gDriveHomingDiag.inputs,ReadDriveHoming,NULL,0}, \
 {NULL,NULL,0x20E0,{DEFTYPE_UNSIGNED16,OBJCODE_VAR << 8},&hmRead16,hmN20E0,(void *)&gDriveHomingDiag.state,ReadDriveHoming,NULL,0}, \
 {NULL,NULL,0x20E1,{DEFTYPE_UNSIGNED32,OBJCODE_VAR << 8},&hmU32,hmN20E1,&gDriveHomingConfig.timeoutMs,ReadDriveHoming,WriteDriveHoming,0}, \
 {NULL,NULL,0x20E2,{DEFTYPE_UNSIGNED32,OBJCODE_VAR << 8},&hmU32,hmN20E2,&gDriveHomingConfig.maximumTravel,ReadDriveHoming,WriteDriveHoming,0}, \
 {NULL,NULL,0x20E3,{DEFTYPE_UNSIGNED16,OBJCODE_VAR << 8},&hmRead16,hmN20E3,(void *)&gDriveHomingDiag.error,ReadDriveHoming,NULL,0}, \
 {NULL,NULL,0x20E4,{DEFTYPE_UNSIGNED32,OBJCODE_VAR << 8},&hmRead32,hmN20E4,(void *)&gDriveHomingDiag.version,ReadDriveHoming,NULL,0}
