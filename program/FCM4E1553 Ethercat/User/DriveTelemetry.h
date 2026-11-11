/**
 * @file    DriveTelemetry.h
 * @brief   驱动遥测接口声明
 * @details 从ADC采样帧生成安全采样数据及对象字典读取快照。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    快照一致性、采样有效性及单位换算保持原实现。
 */
#ifndef DRIVE_TELEMETRY_H
#define DRIVE_TELEMETRY_H
#include <stdint.h>
/* User-selected MT9221CT-10BR5 nominal profile at VCC=5 V.
 * Sensor gain is BEFORE the board's 2/3 divider; zeros are AT the ADC.
 * These defaults are not calibration evidence for a fitted MT9220. */
#define DT_DEFAULT_VREF_MV 3300U
#define DT_DEFAULT_SENSITIVITY_UVA 200000U
#define DT_DEFAULT_ZERO_UV 1666667U
/* User-selected display normalization reference, NOT a motor-current command
 * or a calibration result. PREOP SDO may override it; reset restores it. */
#define DT_DEFAULT_RATED_CURRENT_MA 3000U
#define DT_INTERFACE_VERSION 0x0108U
/* Measurement validity, NOT a calibration certificate, drive enable or safety signal. */
#define DT_VALID_VOLTAGE 0x0001U
#define DT_VALID_PHASES  0x0002U
#define DT_VALID_CURRENT 0x0004U
#define DT_VALID_TORQUE  0x0008U /* reserved, always clear; 0x6077 is not exposed */
#define DT_VALID_BUS_CURRENT 0x0010U /* reserved, always clear; 0x2002 is not exposed */
#define DT_VALID_COMMAND_ERROR 0x0020U /* private 0x2009, PP/CSP only; NOT shaft feedback */
#define DT_ADC_STALE     0x0100U
#define DT_ADC_ERROR     0x0200U
#define DT_ADC_RAIL      0x0400U
#define DT_CURRENT_UNCALIBRATED 0x0800U /* gain=0: conversion disabled/unconfigured */
#define DT_RATED_UNSET   0x1000U
#define DT_INVALID_I16 (-32767 - 1)
#define DT_INVALID_I32 (-2147483647L - 1L)
#define DT_INVALID_U32 0xFFFFFFFFUL
typedef struct {
    uint16_t status; /* 0x2001 */
    int32_t busCurrentMa; /* Reserved storage, always invalid; no public CoE object. */
    int32_t phaseAMa; /* 0x2003 */
    int32_t phaseBMa; /* 0x2004 */
    uint16_t rawBus; /* 0x2005 */
    uint16_t rawA; /* 0x2006 */
    uint16_t rawB; /* 0x2007 */
    uint32_t sampleCount; /* 0x2008 */
    int16_t currentMa; /* 0x2064 */
    uint16_t busVoltageDecivolt; /* 0x2065 */
    uint16_t runState; /* 0x206D */
    uint16_t hardwareVersion; /* 0x206E */
    uint16_t softwareVersion; /* 0x206F */
    int16_t currentPermille; /* 0x6078 */
    uint32_t busVoltageMv; /* 0x6079 */
    int32_t commandPositionError; /* 0x2009: internal target minus executed count, NOT 0x60F4 */
} DriveTelemetryValues;
typedef struct {
    uint32_t vrefMv; /* 0x2010 */
    uint32_t sensitivityUvA; /* 0x2011 */
    uint32_t zeroAUv; /* 0x2012 */
    uint32_t zeroBUv; /* 0x2013 */
    uint32_t ratedCurrentMa; /* 0x6075 */
} DriveTelemetryConfig;
extern DriveTelemetryValues gDriveTelemetry;
extern DriveTelemetryConfig gDriveTelemetryConfig;

/* Watch-only diagnostics. No new CoE objects or PDO layout changes.
 * reason describes the refusal; status/requiredStatus explain missing validity.
 * valid is published last. Records persist until DriveTelemetry_Init/reset. */
#define DT_READ_FAIL_UNSUPPORTED 1U /* legacy reason code, reserved; no active reader */
#define DT_READ_FAIL_INVALID     2U
typedef struct {
    uint32_t valid;
    uint32_t reason;
    uint32_t abortCode;
    uint32_t index;
    uint32_t status;
    uint32_t requiredStatus;
    uint32_t sampleCount;
    uint32_t sampleCycle;
    uint32_t readCycle;
    uint32_t ageCycles;
    uint32_t rawBus;
} DriveTelemetryReadFailure;
typedef struct {
    uint32_t version;
    uint32_t readFailureCount;
    uint32_t voltageFailureCount;
    DriveTelemetryReadFailure firstReadFailure;
    /* Separate latch: other invalid measurements must not hide voltage failures. */
    DriveTelemetryReadFailure firstVoltageFailure;
} DriveTelemetryDiagnostics;
extern volatile DriveTelemetryDiagnostics gDriveTelemetryDiag;

void DriveTelemetry_Init(void);
void DriveTelemetry_AdcComplete(void);
void DriveTelemetry_AdcError(void);
void DriveTelemetry_Update(void);
/* Copy one immutable foreground-published snapshot. No waits in EtherCAT ISR. */
void DriveTelemetry_Latch(void);
void DriveTelemetry_GetSnapshot(DriveTelemetryValues *snapshot);
/* Dedicated read-only raw ADC path for the periodic phase detector. */
typedef struct {
    int32_t aMa, bMa;
    uint32_t sequence;
} DrivePhaseSample;
uint8_t DriveTelemetry_GetPhaseSample(DrivePhaseSample *sample);
/* One coherent, fresh ADC frame for periodic protection and boot calibration. */
typedef struct {
    uint32_t status, sequence, cycle, busMv;
    int32_t aMa, bMa;
    uint16_t rawA, rawB;
} DriveSafetySample;
void DriveTelemetry_GetSafetySample(DriveSafetySample *sample);
#endif
