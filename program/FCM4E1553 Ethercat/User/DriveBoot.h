/**
 * @file    DriveBoot.h
 * @brief   启动零点校准接口声明
 * @details 维护启动校准状态，与遥测、电流及故障模块交换校准结果。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    启动校准与正常电机输出之间的约束保持原实现。
 */
#ifndef DRIVE_BOOT_H
#define DRIVE_BOOT_H
#include "DriveTelemetry.h"
/* Explicitly authorized 24 V bench profile. NOT STO or a production safety function. */
#define DB_OFF 0U
#define DB_WAIT 1U
#define DB_ZERO 2U
#define DB_TEST 3U
#define DB_PASS 4U
#define DB_RESET 5U
#define DB_FAULT 6U
#define DB_TEST_PEAK_MA 1000U
#define DB_TEST_LIMIT_MS 1000U
typedef struct {
    uint32_t version, elapsedMs, zeroSamples, zeroValid;
    uint32_t zeroMinA, zeroMaxA, zeroMinB, zeroMaxB;
    uint32_t zeroAUv, zeroBUv, testMs, goodMs, missingAMs, missingBMs;
    int32_t measuredAMa, measuredBMa;
    uint32_t result, normalReleased;
} DriveBootDiagnostics;
extern volatile uint16_t gDriveBootState; /* 0x2033 */
extern volatile DriveBootDiagnostics gDriveBootDiag;
void DriveBoot_Init(void);
void DriveBoot_Tick(const DriveSafetySample *sample, uint32_t elapsed);
uint8_t DriveBoot_Busy(void);
uint8_t DriveBoot_BlocksNormal(void);
uint8_t DriveBoot_PowerAllowed(void);
uint8_t DriveBoot_RunCycle(void);
void DriveBoot_OnReset(void);
#endif
