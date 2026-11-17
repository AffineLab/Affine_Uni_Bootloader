#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"
#include "hardware/structs/watchdog.h"
#include "board_config.h"
int main(void)
{
    sleep_ms(2000U);
    watchdog_hw->scratch[BOOT_RP2350_REQUEST_SCRATCH] = BOOT_RP2350_REQUEST_MAGIC;
    __dsb();
    watchdog_reboot(0U, 0U, 0U);
    for (;;) tight_loop_contents();
}
