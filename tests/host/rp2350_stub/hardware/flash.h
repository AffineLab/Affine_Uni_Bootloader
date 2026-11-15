#ifndef TEST_RP2350_FLASH_H
#define TEST_RP2350_FLASH_H
#include <stddef.h>
#include <stdint.h>
#define FLASH_PAGE_SIZE 256U
#define FLASH_SECTOR_SIZE 4096U
void flash_range_erase(uint32_t offset, size_t length);
void flash_range_program(uint32_t offset, const uint8_t *data, size_t length);
#endif
