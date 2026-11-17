/* 32 KiB boot region (23620-byte skeleton rounded to 24 KiB + 8 KiB headroom).
 * Its last 4 KiB sector is left for the RP2350-E10 UF2 block at 0x10007F00. */
FLASH(rx) : ORIGIN = 0x10000000, LENGTH = 28K
