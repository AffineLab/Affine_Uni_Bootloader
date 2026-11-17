/* Fixed, NOLOAD RTC slow-memory mailbox shared by independently linked apps.
 * The explicit section reserves the bytes; overlap is a linker error. */
SECTIONS
{
    .affine_rtc_request 0x50001ff8 (NOLOAD) :
    {
        KEEP(*(.affine_rtc_request))
    } > rtc_slow_seg
}
ASSERT(SIZEOF(.affine_rtc_request) == 8, "Affine RTC mailbox must be eight bytes");
ASSERT(_rtc_force_slow_end <= ADDR(.affine_rtc_request), "RTC data overlaps Affine mailbox");
