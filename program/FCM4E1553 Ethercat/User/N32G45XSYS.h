/**
 * @file    N32G45XSYS.h
 * @brief   设备与基础类型依赖接口声明
 * @details 汇集N32G45x设备定义、标准类型及现有基础依赖。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    保留设备头及原类型定义关系。
 */
#ifndef N32G45X_SYS_H
#define N32G45X_SYS_H
/*
 * N32G455 platform declarations shared by all application modules.
 * This is the N32 counterpart of STD245S/GD230SYS.h.
 */
#include "n32g45x.h"
#include "n32g45x_dma.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef RAMRUN
#define RAMRUN __attribute__((section("RAMCODE")))
#endif
#endif
