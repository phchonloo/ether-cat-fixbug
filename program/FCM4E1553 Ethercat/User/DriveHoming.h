/**
 * @file    DriveHoming.h
 * @brief   回零配置与诊断接口声明
 * @details 声明回零配置、诊断状态及现有对外接口。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    回零方法、单位及默认参数保持原实现。
 */
#ifndef DRIVE_HOMING_H
#define DRIVE_HOMING_H
#include <stdint.h>
/* HM values use position counts, count/s and count/s^2, independently of 2000h. */
typedef struct {
    int8_t method;
    int32_t offset;
    uint32_t acceleration;
    uint32_t timeoutMs;
    uint32_t maximumTravel;
} THomingConfig;
#pragma pack(push, 1)
typedef struct {
    uint16_t count;
    uint32_t search;
    uint32_t release;
} THomingSpeeds;
#pragma pack(pop)
typedef struct {
    uint32_t version;
    uint16_t state; /* 0 idle,1 search,2 brake,3 slow reference,4 finish,5 done,6 error brake,7 error,8 cancel */
    uint16_t error; /* 1 config,2 moving start,3 two limits,4 opposite limit,5 timeout,6 travel,7 range,8 negative limit,9 positive limit,10 HOME lost during braking */
    uint32_t inputs; /* 60FD: bit0 X2 negative, bit1 X3 positive, bit2 X1 home; raw X1..4 bits16..19 */
    uint32_t elapsedMs;
    uint32_t startCount;
    int32_t referencePosition;
} THomingDiagnostics;
extern THomingConfig gDriveHomingConfig;
extern THomingSpeeds gDriveHomingSpeeds;
extern volatile THomingDiagnostics gDriveHomingDiag;
#endif
