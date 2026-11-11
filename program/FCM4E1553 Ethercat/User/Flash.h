/**
 * @file    Flash.h
 * @brief   旧Flash兼容接口接口声明
 * @details 保留旧接口入口，并与独立参数保存模块保持现有职责边界。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    现有空操作及选项字节保护行为保持原实现。
 */
#ifndef USER_FLASH_H
#define USER_FLASH_H
#include "mcuinit.h"
#define FlASH_READY_YES (0U)
#define FlASH_READY_NO  (1U)
#define FLASH_READY_YES FlASH_READY_YES
#define FLASH_READY_NO  FlASH_READY_NO
void fmc_erase_pages(void);
void fmc_program(void);
void Flash_ReadInfo(u32 *chI2c_Rxdata, u8 datalen);
void Flash_WriteInfo(u32 *chI2c_Rxdata, u8 datalen);
void Flash_Read_Protect(u8 yesno);
#endif
