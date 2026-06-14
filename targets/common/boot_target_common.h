#ifndef AFFINE_BOOT_TARGET_COMMON_H
#define AFFINE_BOOT_TARGET_COMMON_H

#include "boot_platform.h"

typedef uint8_t (*boot_target_usb_is_busy_fn)(uint8_t ch);
typedef uint8_t (*boot_target_usb_transmit_fn)(uint8_t ch, uint8_t *buf, uint16_t len);

typedef struct
{
    uint16_t length;
    uint8_t data[sizeof(boot_frame_header_t) + BOOT_FRAME_MAX_PAYLOAD];
} boot_target_usb_packet_t;

typedef struct
{
    boot_target_usb_packet_t queue[8];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
    bool in_flight;
    boot_diag_response_t diag;
} boot_target_usb_transport_t;

bool boot_target_flash_range_is_valid(const boot_target_config_t *target, uint32_t address, uint32_t length);
bool boot_target_flash_write_length_is_aligned(const boot_target_config_t *target, uint32_t length);
bool boot_target_flash_write_address_is_aligned(const boot_target_config_t *target, uint32_t address);

void boot_target_usb_transport_note_rx(boot_target_usb_transport_t *transport, uint32_t length, uint32_t now_ms);
void boot_target_usb_transport_note_rx_overflow(boot_target_usb_transport_t *transport);
void boot_target_usb_transport_note_tx_complete(boot_target_usb_transport_t *transport, uint32_t length, uint32_t now_ms);
void boot_target_usb_transport_send(boot_target_usb_transport_t *transport, const uint8_t *data, size_t length);
void boot_target_usb_transport_poll(boot_target_usb_transport_t *transport,
                                    boot_target_usb_is_busy_fn is_busy,
                                    boot_target_usb_transmit_fn transmit,
                                    uint8_t success_status,
                                    uint32_t now_ms);
bool boot_target_usb_transport_get_diag(const boot_target_usb_transport_t *transport, boot_diag_response_t *out_diag);

#endif
