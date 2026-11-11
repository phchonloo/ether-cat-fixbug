/**
 * @file    DriveCurrent.h
 * @brief   电流控制适配接口声明
 * @details 根据运行与保持电流配置衔接原厂电流计算及PWM输出。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    电流缩放与厂商算法库接口保持原实现。
 */
#ifndef DRIVE_CURRENT_H
#define DRIVE_CURRENT_H
#include <stdint.h>
/* Commissioning limits for this board/motor, not an overcurrent protection. */
#define DC_DEFAULT_RUN_MA 3000U
#define DC_MAX_RUN_MA 3000U
#define DC_DEFAULT_HOLD_PERCENT 100U
#define DC_DEFAULT_HOLD_DELAY_MS 200U
#define DC_STATE_OFF 0U
#define DC_STATE_RUN 1U
#define DC_STATE_WAIT 2U
#define DC_STATE_HOLD 3U
typedef struct {
    int16_t runPeakMa; /* 0x20F1, 0=output off, otherwise 100..3000 mA peak */
    uint16_t holdPercent; /* 0x20D5, 10..100 percent of runPeakMa */
    uint16_t holdDelayMs; /* 0x20F5, 1..30000 ms */
} DriveCurrentConfig;

typedef struct {
    uint32_t version;
    uint32_t adapterCalls;
    uint32_t state;
    uint32_t stationaryTicks;
    uint32_t requestedPeakMa;
    uint32_t referencePeakMa; /* command, NOT a measurement */
    int32_t phaseAReferenceCounts;
    int32_t phaseBReferenceCounts;
    uint32_t invalidConfig;
} DriveCurrentDiagnostics;

extern DriveCurrentConfig gDriveCurrentConfig;
extern volatile DriveCurrentDiagnostics gDriveCurrentDiag;
void DriveCurrent_Init(void);
/* Called from TIM8 even when MainFunt skips its current calculation. */
void DriveCurrent_BeginCycle(void);
void DriveCurrent_SelfTestCycle(void);
uint8_t DriveCurrent_OutputAllowed(void);
#endif
