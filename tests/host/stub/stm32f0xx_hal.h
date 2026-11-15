/* Just enough HAL for targets/stm32f072/main.c on the host. */
#ifndef STUB_STM32F0XX_HAL_H
#define STUB_STM32F0XX_HAL_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum { HAL_OK = 0 } HAL_StatusTypeDef;
typedef struct { uint32_t PLLState, PLLSource, PLLMUL, PREDIV; } RCC_PLLInitTypeDef;
typedef struct { uint32_t OscillatorType, HSIState, HSICalibrationValue, HSI48State; RCC_PLLInitTypeDef PLL; } RCC_OscInitTypeDef;
typedef struct { uint32_t ClockType, SYSCLKSource, AHBCLKDivider, APB1CLKDivider; } RCC_ClkInitTypeDef;
typedef struct { uint32_t PeriphClockSelection, UsbClockSelection; } RCC_PeriphCLKInitTypeDef;
typedef struct { uint32_t Prescaler, Source, Polarity, ReloadValue, ErrorLimitValue, HSI48CalibrationValue; } RCC_CRSInitTypeDef;

enum { RCC_OSCILLATORTYPE_HSI = 1, RCC_OSCILLATORTYPE_HSI48 = 2, RCC_HSI_ON = 1, RCC_HSICALIBRATION_DEFAULT = 1,
       RCC_HSI48_ON = 1, RCC_PLL_NONE = 0, RCC_PLL_ON = 1, RCC_PLLSOURCE_HSI = 0, RCC_PLL_MUL6 = 6, RCC_PREDIV_DIV1 = 1,
       RCC_CLOCKTYPE_HCLK = 1, RCC_CLOCKTYPE_SYSCLK = 2, RCC_CLOCKTYPE_PCLK1 = 4, RCC_SYSCLKSOURCE_HSI48 = 1,
       RCC_SYSCLKSOURCE_PLLCLK = 2, RCC_SYSCLK_DIV1 = 1, RCC_HCLK_DIV1 = 1, FLASH_LATENCY_1 = 1, RCC_PERIPHCLK_USB = 1,
       RCC_USBCLKSOURCE_HSI48 = 1, RCC_CRS_SYNC_DIV1 = 1, RCC_CRS_SYNC_SOURCE_USB = 1, RCC_CRS_SYNC_POLARITY_RISING = 1,
       RCC_CRS_ERRORLIMIT_DEFAULT = 1, RCC_CRS_HSI48CALIBRATION_DEFAULT = 1 };

#define __HAL_RCC_CRS_CLK_ENABLE() ((void)0)
#define __HAL_RCC_CRS_RELOADVALUE_CALCULATE(a, b) ((a) / (b))
#define __disable_irq() ((void)0)

static inline HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *o) { (void)o; return HAL_OK; }
static inline HAL_StatusTypeDef HAL_RCC_ClockConfig(RCC_ClkInitTypeDef *c, uint32_t l) { (void)c; (void)l; return HAL_OK; }
static inline HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *c) { (void)c; return HAL_OK; }
static inline void HAL_RCCEx_CRSConfig(RCC_CRSInitTypeDef *c) { (void)c; }
static inline HAL_StatusTypeDef HAL_Init(void) { return HAL_OK; }
uint32_t HAL_GetTick(void);
#endif
