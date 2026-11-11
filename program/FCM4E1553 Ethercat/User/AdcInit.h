/**
 * @file    AdcInit.h
 * @brief   模拟量采样接口声明
 * @details 配置ADC与DMA采样，并由DMA完成中断发布采样结果。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-09-17
 * @note    采样通道及引脚沿用现有板级配置。
 */
#ifndef _AdcInit_H_
#define _AdcInit_H_
#include "mcuinit.h"
void adc_config(void);
void dma_config(void);

extern u32 adc_value[10];
#endif
