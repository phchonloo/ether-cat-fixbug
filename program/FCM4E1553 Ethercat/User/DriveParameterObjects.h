/**
 * @file    DriveParameterObjects.h
 * @brief   参数持久化对象字典片段
 * @details 通过CoE接口请求保存，并在前台执行参数快照、CRC校验和双页Flash提交。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    按工程约定不添加头文件保护，保持现有SSC集成方式。
 */
#include "DriveParameters.h"
#include "DriveSafety.h"
UINT8 ReadDriveParameters(UINT16, UINT8, UINT32, UINT16 MBXMEM *, UINT8);
UINT8 WriteDriveParameters(UINT16, UINT8, UINT32, UINT16 MBXMEM *, UINT8);
typedef struct OBJ_STRUCT_PACKED_START {
    UINT16 count;
    UINT32 value;
} OBJ_STRUCT_PACKED_END DriveStoreObject;
DriveStoreObject gDriveStoreObject = {1U, 1U}, gDriveRestoreObject = {1U, 1U};
OBJCONST TSDOINFOENTRYDESC OBJMEM dpStoreDesc[] = {
    {DEFTYPE_UNSIGNED8, 8, ACCESS_READ},
    {DEFTYPE_UNSIGNED32, 32, ACCESS_READ | ACCESS_WRITE_PREOP}};
OBJCONST TSDOINFOENTRYDESC OBJMEM dpDiagDesc = {DEFTYPE_UNSIGNED32, 32, ACCESS_READ};
OBJCONST UCHAR OBJMEM dpStoreName[] = "Store parameters\000All supported parameters\000\377";
OBJCONST UCHAR OBJMEM dpRestoreName[] = "Restore factory parameters on next boot\000All supported parameters\000\377";
OBJCONST UCHAR OBJMEM dpNameC0[] = "Parameter command status 0 idle 1 busy 2 saved 3 unchanged 4 failed 5 restore saved";
OBJCONST UCHAR OBJMEM dpNameC1[] = "Stored parameter generation";
OBJCONST UCHAR OBJMEM dpNameC2[] = "Loaded parameter slot 0 defaults 1 A 2 B";
OBJCONST UCHAR OBJMEM dpNameC3[] = "Parameter command error";
OBJCONST UCHAR OBJMEM dpNameC4[] = "Unsaved supported parameters";
OBJCONST UCHAR OBJMEM dpNameC5[] = "Restart interlock requires fresh disable RxPDO";
OBJCONST UCHAR OBJMEM dpNameC6[] = "MCU reset flags captured at boot";
OBJCONST UCHAR OBJMEM dpNameC7[] = "Directional limit deceleration active";
OBJCONST UCHAR OBJMEM dpNameC8[] = "Protection firmware interface version";
OBJCONST UCHAR OBJMEM dpNameC9[] = "Valid parameter page mask";
#define DRIVE_PARAMETER_OD_ENTRIES \
 {NULL,NULL,0x1010,{DEFTYPE_UNSIGNED32,1 | (OBJCODE_ARR << 8)},dpStoreDesc,dpStoreName,&gDriveStoreObject,ReadDriveParameters,WriteDriveParameters,0}, \
 {NULL,NULL,0x1011,{DEFTYPE_UNSIGNED32,1 | (OBJCODE_ARR << 8)},dpStoreDesc,dpRestoreName,&gDriveRestoreObject,ReadDriveParameters,WriteDriveParameters,0}, \
 {NULL,NULL,0x20C0,{DEFTYPE_UNSIGNED32,OBJCODE_VAR << 8},&dpDiagDesc,dpNameC0,(void *)&gDriveParameterDiag.status,ReadDriveParameters,NULL,0}, \
 {NULL,NULL,0x20C1,{DEFTYPE_UNSIGNED32,OBJCODE_VAR << 8},&dpDiagDesc,dpNameC1,(void *)&gDriveParameterDiag.generation,ReadDriveParameters,NULL,0}, \
 {NULL,NULL,0x20C2,{DEFTYPE_UNSIGNED32,OBJCODE_VAR << 8},&dpDiagDesc,dpNameC2,(void *)&gDriveParameterDiag.loadedSlot,ReadDriveParameters,NULL,0}, \
 {NULL,NULL,0x20C3,{DEFTYPE_UNSIGNED32,OBJCODE_VAR << 8},&dpDiagDesc,dpNameC3,(void *)&gDriveParameterDiag.error,ReadDriveParameters,NULL,0}, \
 {NULL,NULL,0x20C4,{DEFTYPE_UNSIGNED32,OBJCODE_VAR << 8},&dpDiagDesc,dpNameC4,(void *)&gDriveParameterDiag.dirty,ReadDriveParameters,NULL,0}, \
 {NULL,NULL,0x20C5,{DEFTYPE_UNSIGNED32,OBJCODE_VAR << 8},&dpDiagDesc,dpNameC5,(void *)&gDriveSafetyDiag.restartInhibit,ReadDriveParameters,NULL,0}, \
 {NULL,NULL,0x20C6,{DEFTYPE_UNSIGNED32,OBJCODE_VAR << 8},&dpDiagDesc,dpNameC6,(void *)&gDriveSafetyDiag.resetFlags,ReadDriveParameters,NULL,0}, \
 {NULL,NULL,0x20C7,{DEFTYPE_UNSIGNED32,OBJCODE_VAR << 8},&dpDiagDesc,dpNameC7,(void *)&gDriveSafetyDiag.limitStopping,ReadDriveParameters,NULL,0}, \
 {NULL,NULL,0x20C8,{DEFTYPE_UNSIGNED32,OBJCODE_VAR << 8},&dpDiagDesc,dpNameC8,(void *)&gDriveSafetyDiag.version,ReadDriveParameters,NULL,0}, \
 {NULL,NULL,0x20C9,{DEFTYPE_UNSIGNED32,OBJCODE_VAR << 8},&dpDiagDesc,dpNameC9,(void *)&gDriveParameterDiag.validMask,ReadDriveParameters,NULL,0}
