/**
 * @file    DriveFaultObjects.h
 * @brief   驱动故障管理对象字典片段
 * @details 汇总采样、电流和通信故障，并维护故障锁存与复位条件。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    按工程约定不添加头文件保护，保持现有SSC集成方式。
 */
/* Single-axis diagnostics; keep 1A00 and 1A03 byte-for-byte unchanged. */
#include "DriveFault.h"
#include "DriveBoot.h"
UINT8 WriteDriveFault(UINT16, UINT8, UINT32, UINT16 MBXMEM *, UINT8);
UINT8 ReadDriveFaultReset(UINT16, UINT8, UINT32, UINT16 MBXMEM *, UINT8);
OBJCONST TSDOINFOENTRYDESC OBJMEM dfConfigDesc =
    {DEFTYPE_UNSIGNED16, 16, ACCESS_READ | ACCESS_WRITE_PREOP};
OBJCONST TSDOINFOENTRYDESC OBJMEM dfValueDesc =
    {DEFTYPE_UNSIGNED16, 16, ACCESS_READ | OBJACCESS_TXPDOMAPPING};
OBJCONST UCHAR OBJMEM dfName2020[] = "Current fault detectors (bit0 OC, bit1 stationary phase; default 2)";
OBJCONST UCHAR OBJMEM dfName2021[] = "Software overcurrent threshold per phase mA";
OBJCONST UCHAR OBJMEM dfName2030[] = "Latched fault reason";
OBJCONST UCHAR OBJMEM dfName2031[] = "Motor fault reset conditions ready (not a reset command)";
OBJCONST UCHAR OBJMEM dfName2032[] = "Motor fault reset inhibit mask";
OBJCONST UCHAR OBJMEM dfName2033[] = "Boot self-test (1 wait,2 zero,3 powered,4 passed,5 reset,6 fault)";
typedef struct OBJ_STRUCT_PACKED_START {
    UINT16 count;
    UINT32 entries[9];
} OBJ_STRUCT_PACKED_END DriveFaultPdo;
DriveFaultPdo gDriveFaultPdo = {9, {0x60410010, 0x60640020, 0x606C0020, 0x60610008, 0x00000008, 0x603F0010, 0x206C0010, 0x20300010, 0x20310010}};
OBJCONST TSDOINFOENTRYDESC OBJMEM dfPdoDesc[] = {
    {DEFTYPE_UNSIGNED8, 8, ACCESS_READ},
    {DEFTYPE_UNSIGNED32, 32, ACCESS_READ}, {DEFTYPE_UNSIGNED32, 32, ACCESS_READ},
    {DEFTYPE_UNSIGNED32, 32, ACCESS_READ}, {DEFTYPE_UNSIGNED32, 32, ACCESS_READ},
    {DEFTYPE_UNSIGNED32, 32, ACCESS_READ}, {DEFTYPE_UNSIGNED32, 32, ACCESS_READ},
    {DEFTYPE_UNSIGNED32, 32, ACCESS_READ}, {DEFTYPE_UNSIGNED32, 32, ACCESS_READ},
    {DEFTYPE_UNSIGNED32, 32, ACCESS_READ}};
OBJCONST UCHAR OBJMEM dfPdoName[] = "Drive feedback with fault codes\000\377";
#define DRIVE_FAULT_OD_ENTRIES \
    {NULL,NULL,0x1A04,{DEFTYPE_PDOMAPPING,9 | (OBJCODE_REC << 8)},dfPdoDesc,dfPdoName,&gDriveFaultPdo,NULL,NULL,0}, \
    {NULL,NULL,0x2020,{DEFTYPE_UNSIGNED16,OBJCODE_VAR << 8},&dfConfigDesc,dfName2020,&gDriveFaultConfig.currentEnable,NULL,WriteDriveFault,0}, \
    {NULL,NULL,0x2021,{DEFTYPE_UNSIGNED16,OBJCODE_VAR << 8},&dfConfigDesc,dfName2021,&gDriveFaultConfig.overcurrentMa,NULL,WriteDriveFault,0}, \
    {NULL,NULL,0x2030,{DEFTYPE_UNSIGNED16,OBJCODE_VAR << 8},&dfValueDesc,dfName2030,(void *)&gDriveFaultReason,NULL,NULL,0}, \
    {NULL,NULL,0x2031,{DEFTYPE_UNSIGNED16,OBJCODE_VAR << 8},&dfValueDesc,dfName2031,(void *)&gDriveFaultResetReady,ReadDriveFaultReset,NULL,0}, \
    {NULL,NULL,0x2032,{DEFTYPE_UNSIGNED16,OBJCODE_VAR << 8},&dfValueDesc,dfName2032,(void *)&gDriveFaultResetBlockMask,ReadDriveFaultReset,NULL,0}, \
    {NULL,NULL,0x2033,{DEFTYPE_UNSIGNED16,OBJCODE_VAR << 8},&dfValueDesc,dfName2033,(void *)&gDriveBootState,NULL,NULL,0}
