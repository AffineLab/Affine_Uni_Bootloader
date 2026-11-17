#ifndef CH32_HOST_STUB_H
#define CH32_HOST_STUB_H

#include <stdint.h>

typedef enum
{
    FLASH_COMPLETE, FLASH_ERROR_WRP, FLASH_TIMEOUT, FLASH_BUSY
} FLASH_Status;
#define FLASH_FLAG_EOP 32U
#define FLASH_STATR_PGERR 0x04U
#define FLASH_CTLR_LOCK 0x80U
#define FLASH_CTLR_FAST_LOCK 0x8000U

typedef struct
{
    volatile uint32_t KEYR;
    volatile uint32_t MODEKEYR;
    volatile uint32_t STATR;
    volatile uint32_t CTLR;
} FLASH_Type;

extern FLASH_Type host_flash;
#define FLASH (&host_flash)
#define FLASH_FLAG_WRPRTERR 16U
void FLASH_Unlock(void);
void FLASH_Lock(void);
void FLASH_ClearFlag(uint32_t flags);
FLASH_Status FLASH_ErasePage(uint32_t address);
FLASH_Status FLASH_ProgramHalfWord(uint32_t address, uint16_t data);
void FLASH_ProgramPage_Fast(uint32_t address, uint32_t *data);
void FLASH_Lock_Fast(void);
FLASH_Status FLASH_GetStatus(void);
typedef struct
{
    volatile uint32_t CTLR;
    volatile uint32_t SR;
    volatile uint64_t CNT;
    volatile uint64_t CMP;
} SysTick_Type;
typedef struct
{
    volatile uint32_t IRER[8];
    volatile uint32_t IPRR[8];
    volatile uint32_t VTFADDR[4];
} PFIC_Type;
typedef struct
{
    volatile uint32_t EXTEN_CTR;
} EXTEN_Type;
typedef struct
{
    volatile uint8_t BASE_CTRL;
} USBFSD_Type;
extern SysTick_Type host_systick;
extern PFIC_Type host_pfic;
extern EXTEN_Type host_exten;
extern USBFSD_Type host_usbfs;
#define SysTick (&host_systick)
#define PFIC (&host_pfic)
#define EXTEN (&host_exten)
#define USBFSD (&host_usbfs)
#define EXTEN_USBD_PU_EN 0x00000002U
void __disable_irq(void);
void RCC_DeInit(void);

#endif
