#include "targets/common/boot_target_common.h"

#include <string.h>

bool boot_target_flash_range_is_valid(const boot_target_config_t *target, uint32_t address, uint32_t length)
{
    uint32_t flash_end;

    if (target == NULL)
    {
        return false;
    }

    if (length == 0U)
    {
        return true;
    }

    flash_end = target->flash_base + target->flash_size;

    if (address < target->flash_base)
    {
        return false;
    }

    if (address >= flash_end)
    {
        return false;
    }

    return length <= (flash_end - address);
}

bool boot_target_flash_write_length_is_aligned(const boot_target_config_t *target, uint32_t length)
{
    if ((target == NULL) || (target->write_size == 0U))
    {
        return false;
    }

    return (length % target->write_size) == 0U;
}

bool boot_target_flash_write_address_is_aligned(const boot_target_config_t *target, uint32_t address)
{
    if ((target == NULL) || (target->write_size == 0U) || (address < target->flash_base))
    {
        return false;
    }

    return ((address - target->flash_base) % target->write_size) == 0U;
}

void boot_target_usb_transport_note_rx(boot_target_usb_transport_t *transport, uint32_t length, uint32_t now_ms)
{
    if (transport == NULL)
    {
        return;
    }

    transport->diag.rx_callback_count++;
    transport->diag.rx_byte_count += length;
    transport->diag.rx_last_len = length;
    transport->diag.last_rx_tick_ms = now_ms;
}

void boot_target_usb_transport_note_rx_overflow(boot_target_usb_transport_t *transport)
{
    if (transport == NULL)
    {
        return;
    }

    transport->diag.rx_fifo_overflow_count++;
}

void boot_target_usb_transport_note_tx_complete(boot_target_usb_transport_t *transport, uint32_t length, uint32_t now_ms)
{
    if (transport == NULL)
    {
        return;
    }

    transport->diag.tx_complete_count++;
    transport->diag.tx_last_len = length;
    transport->diag.last_tx_complete_tick_ms = now_ms;
}

void boot_target_usb_transport_send(boot_target_usb_transport_t *transport, const uint8_t *data, size_t length)
{
    boot_target_usb_packet_t *packet;

    if (transport == NULL)
    {
        return;
    }

    if (((data == NULL) && (length > 0U)) || (length > sizeof(transport->queue[0].data)) ||
        (transport->count >= (sizeof(transport->queue) / sizeof(transport->queue[0]))))
    {
        transport->diag.tx_queue_drop_count++;
        return;
    }

    packet = &transport->queue[transport->head];
    packet->length = (uint16_t)length;
    memcpy(packet->data, data, length);

    transport->head = (uint8_t)((transport->head + 1U) % (sizeof(transport->queue) / sizeof(transport->queue[0])));
    transport->count++;
    transport->diag.tx_enqueue_count++;
}

void boot_target_usb_transport_poll(boot_target_usb_transport_t *transport,
                                    boot_target_usb_is_busy_fn is_busy,
                                    boot_target_usb_transmit_fn transmit,
                                    uint8_t success_status,
                                    uint32_t now_ms)
{
    if ((transport == NULL) || (is_busy == NULL) || (transmit == NULL))
    {
        return;
    }

    if (transport->in_flight)
    {
        if (is_busy(0U) != 0U)
        {
            return;
        }

        transport->tail = (uint8_t)((transport->tail + 1U) % (sizeof(transport->queue) / sizeof(transport->queue[0])));
        transport->count--;
        transport->in_flight = false;
    }

    if ((transport->count == 0U) || (is_busy(0U) != 0U))
    {
        return;
    }

    if (transmit(0U, transport->queue[transport->tail].data, transport->queue[transport->tail].length) == success_status)
    {
        transport->in_flight = true;
        transport->diag.tx_start_count++;
        transport->diag.tx_last_len = transport->queue[transport->tail].length;
        transport->diag.last_tx_start_tick_ms = now_ms;
    }
    else
    {
        transport->diag.tx_busy_reject_count++;
    }
}

bool boot_target_usb_transport_get_diag(const boot_target_usb_transport_t *transport, boot_diag_response_t *out_diag)
{
    if ((transport == NULL) || (out_diag == NULL))
    {
        return false;
    }

    *out_diag = transport->diag;
    return true;
}
