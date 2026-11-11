/**
 * @file    DriveParameters.h
 * @brief   参数持久化接口声明
 * @details 通过CoE接口请求保存，并在前台执行参数快照、CRC校验和双页Flash提交。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    参数记录布局与提交顺序保持原实现。
 */
#ifndef DRIVE_PARAMETERS_H
#define DRIVE_PARAMETERS_H
#include <stdint.h>
#define DP_SLOT_A 0x0803F000UL
#define DP_SLOT_B 0x0803F800UL
#define DP_PAGE_SIZE 0x800U
#define DP_SCHEMA 0x00010001UL
#define DP_IDLE 0U
#define DP_PENDING 1U
#define DP_SAVED 2U
#define DP_UNCHANGED 3U
#define DP_FAILED 4U
#define DP_RESTORE_SAVED 5U
typedef struct {
    uint32_t version, status, loadedSlot, generation, validMask;
    uint32_t saveCount, failedCount, error, dirty, restorePending;
} DriveParameterDiagnostics;
extern volatile DriveParameterDiagnostics gDriveParameterDiag;
void DriveParameters_Init(void); /* after defaults, before PWM/interrupt start */
void DriveParameters_Update(void); /* PREOP/disabled foreground only */
uint8_t DriveParameters_Busy(void);
uint8_t DriveParameters_CanWrite(void);
uint8_t DriveParameters_PowerHealthy(void);
uint32_t DriveParameters_Request(uint8_t restore);
#endif
