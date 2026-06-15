#!/usr/bin/env python3
"""Minimal host-side test tool for the Affine USB-CDC bootloader."""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import secrets
import struct
import subprocess
import sys
import tempfile
import time
import zlib

serial = None
list_ports = None


def require_pyserial() -> None:
    global serial, list_ports

    if serial is not None and list_ports is not None:
        return

    try:
        import serial as serial_module
        from serial.tools import list_ports as list_ports_module
    except ModuleNotFoundError as exc:  # pragma: no cover - operator guidance
        raise SystemExit(
            "pyserial is required for serial I/O. Install it with: py -m pip install pyserial"
        ) from exc

    serial = serial_module
    list_ports = list_ports_module


BOOT_FRAME_MAGIC = 0x42464641
BOOT_PROTOCOL_VERSION = 0x00010004

BOOT_OP_HELLO = 0x0001
BOOT_OP_BEGIN = 0x0002
BOOT_OP_DATA = 0x0003
BOOT_OP_COMMIT = 0x0004
BOOT_OP_ABORT = 0x0005
BOOT_OP_GET_STATUS = 0x0006
BOOT_OP_BOOT_APP = 0x0007
BOOT_OP_GET_DIAG = 0x0008
BOOT_OP_GET_METADATA = 0x0009
BOOT_OP_CLEAR_METADATA = 0x000A
BOOT_OP_REBOOT = 0x000B
BOOT_OP_VERIFY_IMAGE = 0x000C
BOOT_OP_SET_MANIFEST = 0x000D
BOOT_OP_GET_DEVICE_INFO = 0x000E
BOOT_OP_RESPONSE = 0x8000

BOOT_FRAME_MAX_PAYLOAD = 512
BOOT_MANIFEST_MAGIC = 0x4D464641
BOOT_MANIFEST_VERSION = 2
BOOT_MANIFEST_SIZE = 364
BOOT_MANIFEST_NONCE_OFFSET = 72
BOOT_DATA_PREFIX_SIZE = 8
BOOT_DEFAULT_WRITE_ALIGN = 8
BOOT_FLAG_VERIFY_SIGNATURE = 1 << 0
BOOT_FLAG_ENCRYPTED_IMAGE = 1 << 1
BOOT_FLAG_ANTI_ROLLBACK = 1 << 2
BOOT_DEV_AES128_KEY = bytes.fromhex("fbe7832d34ef7c5406de5f8329515ab9")

BOOT_CAP_NAMES = {
    0: "VERIFY_SIGNATURE",
    1: "ENCRYPTED_STREAM",
    2: "ANTI_ROLLBACK",
    3: "UNSIGNED_UPDATES",
}

BOOT_FLAG_NAMES = {
    0: "VERIFY_SIGNATURE",
    1: "ENCRYPTED_IMAGE",
    2: "ANTI_ROLLBACK",
}

APP_CMD_JUMP_TO_BOOTLOADER = 0x25
DEFAULT_USB_VID = 0xAFF1
DEFAULT_G431_BOOTLOADER_PID = 0x52B0
DEFAULT_APP_PID = 0x52A6

TARGET_DEFAULTS = {
    0x46303732: {
        "name": "stm32f072/monica_nfc",
        "board_id": 0x4D4F4E43,
        "flash_layout_id": 0x46303741,
        "bootloader_pid": 0x52B2,
        "write_align": 2,
    },
    0x47343331: {
        "name": "stm32g431/mai",
        "board_id": 0x4D414931,
        "flash_layout_id": 0x47343341,
        "bootloader_pid": 0x52B0,
        "write_align": 8,
    },
    0x48353033: {
        "name": "stm32h503/linneapro",
        "board_id": 0x4C504835,
        "flash_layout_id": 0x48353041,
        "bootloader_pid": 0x52B1,
        "write_align": 16,
    },
}

BOOT_STATUS_NAMES = {
    0: "IDLE",
    1: "READY",
    2: "RECEIVING",
    3: "COMMITTED",
    4: "ERROR",
    5: "ERASING",
    6: "VERIFYING",
}

BOOT_OPERATION_NAMES = {
    0: "NONE",
    1: "ERASE_APP",
    2: "ERASE_METADATA",
    3: "WRITE_DATA",
    4: "VERIFY_IMAGE",
    5: "STORE_METADATA",
    6: "CLEAR_METADATA",
    7: "REBOOT",
    8: "BOOT_APP",
}

BOOT_ERROR_NAMES = {
    0: "NONE",
    1: "BAD_MAGIC",
    2: "BAD_HEADER_CRC",
    3: "BAD_PAYLOAD_CRC",
    4: "UNSUPPORTED_OPCODE",
    5: "BAD_STATE",
    6: "BAD_TARGET",
    7: "RANGE",
    8: "ALIGNMENT",
    9: "FLASH",
    10: "IMAGE_TOO_LARGE",
    11: "IMAGE_CRC",
    12: "NO_VALID_APP",
    13: "MANIFEST",
    14: "SIGNATURE",
    15: "ROLLBACK",
    16: "DECRYPTION",
}


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def format_hex(data: bytes) -> str:
    return " ".join(f"{byte:02X}" for byte in data)


def format_bit_names(value: int, names: dict[int, str]) -> str:
    active = [name for bit, name in names.items() if (value & (1 << bit)) != 0]
    return "|".join(active) if active else "none"


def build_frame(opcode: int, sequence: int, payload: bytes = b"") -> bytes:
    header_without_crc = struct.pack(
        "<IHHII",
        BOOT_FRAME_MAGIC,
        opcode,
        sequence,
        len(payload),
        crc32(payload),
    )
    return header_without_crc + struct.pack("<I", crc32(header_without_crc)) + payload


def build_app_command(command: int, payload: bytes = b"") -> bytes:
    frame = bytes([0xFF, command & 0xFF, len(payload) & 0xFF]) + payload
    checksum = sum(frame) & 0xFF
    return frame + bytes([checksum])


def parse_hex_bytes(value: str, expected_size: int) -> bytes:
    text = value.strip().replace(":", "").replace(" ", "")
    data = bytes.fromhex(text)
    if len(data) != expected_size:
        raise argparse.ArgumentTypeError(f"expected {expected_size} bytes, got {len(data)}")
    return data


def sign_manifest_payload(signed_region: bytes, private_key: pathlib.Path) -> bytes:
    with tempfile.TemporaryDirectory() as tmpdir:
        region_path = pathlib.Path(tmpdir) / "manifest-signed-region.bin"
        sig_path = pathlib.Path(tmpdir) / "manifest.sig"
        region_path.write_bytes(signed_region)
        subprocess.run(
            [
                "openssl",
                "dgst",
                "-sha256",
                "-sign",
                str(private_key),
                "-out",
                str(sig_path),
                str(region_path),
            ],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        signature = sig_path.read_bytes()
    if len(signature) != 256:
        raise RuntimeError(f"unexpected RSA-2048 signature length: {len(signature)}")
    return signature


def build_signed_manifest(
    target_id: int,
    board_id: int,
    flash_layout_id: int,
    image: bytes,
    image_crc: int,
    version: int,
    flags: int,
    key_id: int,
    nonce: bytes,
    private_key: pathlib.Path,
) -> bytes:
    signed_region = struct.pack(
        "<10I32s16s5I",
        BOOT_MANIFEST_MAGIC,
        BOOT_MANIFEST_VERSION,
        target_id,
        board_id,
        flash_layout_id,
        len(image),
        image_crc,
        version,
        flags,
        key_id,
        hashlib.sha256(image).digest(),
        nonce,
        0,
        0,
        0,
        0,
        0,
    )
    signature = sign_manifest_payload(signed_region, private_key)
    manifest = signed_region + signature
    if len(manifest) != BOOT_MANIFEST_SIZE:
        raise RuntimeError(f"internal manifest size mismatch: {len(manifest)}")
    return manifest


def parse_manifest_header(manifest: bytes) -> dict[str, int]:
    if len(manifest) != BOOT_MANIFEST_SIZE:
        raise RuntimeError(f"manifest size mismatch: {len(manifest)}")
    (
        magic,
        manifest_version,
        target_id,
        board_id,
        flash_layout_id,
        image_size,
        image_crc32,
        firmware_version,
        flags,
        key_id,
    ) = struct.unpack_from("<10I", manifest, 0)
    return {
        "magic": magic,
        "manifest_version": manifest_version,
        "target_id": target_id,
        "board_id": board_id,
        "flash_layout_id": flash_layout_id,
        "image_size": image_size,
        "image_crc32": image_crc32,
        "firmware_version": firmware_version,
        "flags": flags,
        "key_id": key_id,
    }


def aes128_ctr_crypt(data: bytes, key: bytes, nonce: bytes) -> bytes:
    with tempfile.TemporaryDirectory() as tmpdir:
        in_path = pathlib.Path(tmpdir) / "plain.bin"
        out_path = pathlib.Path(tmpdir) / "cipher.bin"
        in_path.write_bytes(data)
        subprocess.run(
            [
                "openssl",
                "enc",
                "-aes-128-ctr",
                "-K",
                key.hex(),
                "-iv",
                nonce.hex(),
                "-nopad",
                "-in",
                str(in_path),
                "-out",
                str(out_path),
            ],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        return out_path.read_bytes()


def read_exact(port: serial.Serial, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = port.read(size - len(data))
        if not chunk:
            raise TimeoutError(f"timed out waiting for {size} bytes, got {len(data)}")
        data.extend(chunk)
    return bytes(data)


def read_frame(port: serial.Serial) -> tuple[tuple[int, int, int, int, int, int], bytes, bytes]:
    header = read_exact(port, 20)
    fields = struct.unpack("<IHHIII", header)
    magic, opcode, sequence, payload_len, payload_crc, header_crc = fields

    if magic != BOOT_FRAME_MAGIC:
        raise ValueError(f"bad magic 0x{magic:08X}")
    if crc32(header[:16]) != header_crc:
        raise ValueError("bad header crc")

    payload = read_exact(port, payload_len)
    if crc32(payload) != payload_crc:
        raise ValueError("bad payload crc")

    return fields, payload, header + payload


def open_serial_port(
    port_name: str,
    baud: int,
    timeout: float,
    open_retries: int,
    retry_interval: float,
) -> serial.Serial:
    require_pyserial()
    last_error: Exception | None = None
    for attempt in range(1, open_retries + 1):
        try:
            return serial.Serial(
                port_name,
                baud,
                timeout=timeout,
                write_timeout=timeout,
            )
        except serial.SerialException as exc:
            last_error = exc
            if attempt < open_retries:
                time.sleep(retry_interval)
                continue
            break

    raise RuntimeError(f"failed to open {port_name}: {last_error}") from last_error


def parse_int_auto(value: str) -> int:
    return int(value, 0)


def describe_port(port_info: object) -> str:
    vid = getattr(port_info, "vid", None)
    pid = getattr(port_info, "pid", None)
    serial_number = getattr(port_info, "serial_number", None)
    identity = []

    if vid is not None and pid is not None:
        identity.append(f"VID:PID={vid:04X}:{pid:04X}")
    if serial_number:
        identity.append(f"SER={serial_number}")

    suffix = f" ({', '.join(identity)})" if identity else ""
    return f"{port_info.device}{suffix}"


def find_port_info(port_name: str) -> object | None:
    require_pyserial()
    normalized = port_name.lower()
    for port_info in list_ports.comports():
        if port_info.device.lower() == normalized:
            return port_info
    return None


def port_matches(port_info: object, vid: int | None, pid: int | None, serial_number: str | None) -> bool:
    if vid is not None and getattr(port_info, "vid", None) != vid:
        return False
    if pid is not None and getattr(port_info, "pid", None) != pid:
        return False
    if serial_number:
        port_serial = getattr(port_info, "serial_number", None)
        if port_serial is None or port_serial.lower() != serial_number.lower():
            return False
    return True


def wait_for_usb_port(
    vid: int | None,
    pid: int | None,
    serial_number: str | None,
    timeout: float,
    label: str,
) -> object:
    require_pyserial()
    deadline = time.monotonic() + timeout
    last_seen = "none"

    while time.monotonic() < deadline:
        ports = list(list_ports.comports())
        matches = [port_info for port_info in ports if port_matches(port_info, vid, pid, serial_number)]
        if matches:
            port_info = matches[0]
            print(f"{label} port: {describe_port(port_info)}")
            return port_info
        if ports:
            last_seen = ", ".join(describe_port(port_info) for port_info in ports)
        time.sleep(0.1)

    raise RuntimeError(f"timed out waiting for {label} USB port, last seen: {last_seen}")


def resolve_wait_serial(port_name: str, explicit_serial: str | None) -> str | None:
    if explicit_serial:
        return explicit_serial

    port_info = find_port_info(port_name)
    if port_info is None:
        return None

    return getattr(port_info, "serial_number", None)


def parse_hello_fields(payload: bytes) -> tuple[int, int, int, int, int, int, int]:
    if len(payload) != 28:
        raise ValueError(f"unexpected HELLO payload length: {len(payload)}")
    return struct.unpack("<7I", payload)


def parse_status_fields(payload: bytes) -> tuple[int, int, int, int, int, int, int, int]:
    if len(payload) == 20:
        status, last_error, written_size, expected_size, running_crc32 = struct.unpack("<5I", payload)
        return status, last_error, written_size, expected_size, running_crc32, 0, 0, 0

    if len(payload) != 32:
        raise ValueError(f"unexpected STATUS payload length: {len(payload)}")
    return struct.unpack("<8I", payload)


def decode_hello(payload: bytes) -> str:
    protocol_version, target_id, flash_size, app_base, slot_size, max_chunk_size, capabilities = parse_hello_fields(
        payload
    )
    return (
        f"protocol=0x{protocol_version:08X}, "
        f"target=0x{target_id:08X}, "
        f"flash_size={flash_size}, "
        f"app_base=0x{app_base:08X}, "
        f"slot_size=0x{slot_size:08X}, "
        f"max_chunk_size={max_chunk_size}, "
        f"capabilities=0x{capabilities:08X}({format_bit_names(capabilities, BOOT_CAP_NAMES)})"
    )


def parse_device_info_fields(payload: bytes) -> tuple[int, int, int, int, int, int, int, int, int, int, int, int, int, int]:
    if len(payload) != 56:
        raise ValueError(f"unexpected DEVICE_INFO payload length: {len(payload)}")
    return struct.unpack("<14I", payload)


def decode_device_info(payload: bytes) -> str:
    (
        protocol_version,
        target_id,
        board_id,
        board_revision,
        flash_layout_id,
        flash_base,
        flash_size,
        app_base,
        metadata_base,
        metadata_size,
        erase_size,
        write_size,
        max_chunk_size,
        capabilities,
    ) = parse_device_info_fields(payload)
    return (
        f"protocol=0x{protocol_version:08X}, "
        f"target=0x{target_id:08X}, "
        f"board=0x{board_id:08X}, "
        f"board_revision={board_revision}, "
        f"layout=0x{flash_layout_id:08X}, "
        f"flash_base=0x{flash_base:08X}, "
        f"flash_size={flash_size}, "
        f"app_base=0x{app_base:08X}, "
        f"metadata_base=0x{metadata_base:08X}, "
        f"metadata_size=0x{metadata_size:08X}, "
        f"erase_size={erase_size}, "
        f"write_size={write_size}, "
        f"max_chunk_size={max_chunk_size}, "
        f"capabilities=0x{capabilities:08X}({format_bit_names(capabilities, BOOT_CAP_NAMES)})"
    )


def decode_status(payload: bytes) -> str:
    (
        status,
        last_error,
        written_size,
        expected_size,
        running_crc32,
        operation,
        operation_progress,
        operation_total,
    ) = parse_status_fields(payload)
    return (
        f"status={status}({BOOT_STATUS_NAMES.get(status, 'UNKNOWN')}), "
        f"last_error={last_error}({BOOT_ERROR_NAMES.get(last_error, 'UNKNOWN')}), "
        f"written_size={written_size}, "
        f"expected_size={expected_size}, "
        f"running_crc32=0x{running_crc32:08X}, "
        f"operation={operation}({BOOT_OPERATION_NAMES.get(operation, 'UNKNOWN')}), "
        f"operation_progress={operation_progress}/{operation_total}"
    )


def decode_metadata(payload: bytes) -> str:
    if len(payload) == 80:
        fields = struct.unpack("<20I", payload)
        valid, copy_count, selected_copy, app_valid = fields[:4]
        (
            magic,
            target_id,
            version,
            image_size,
            image_crc32,
            app_base,
            flags,
            metadata_crc32,
            *reserved,
        ) = fields[4:]
        selected = "none" if selected_copy == 0xFFFFFFFF else str(selected_copy)

        return (
            f"valid={valid}, "
            f"copy_count={copy_count}, "
            f"selected_copy={selected}, "
            f"app_vector_valid={app_valid}, "
            f"magic=0x{magic:08X}, "
            f"target=0x{target_id:08X}, "
            f"version={version}, "
            f"image_size={image_size}, "
            f"image_crc32=0x{image_crc32:08X}, "
            f"app_base=0x{app_base:08X}, "
            f"flags=0x{flags:08X}({format_bit_names(flags, BOOT_FLAG_NAMES)}), "
            f"metadata_crc32=0x{metadata_crc32:08X}"
        )

    if len(payload) == 368:
        (
            valid,
            copy_count,
            selected_copy,
            app_valid,
            magic,
            target_id,
            board_id,
            flash_layout_id,
            version,
            image_size,
            image_crc32,
            app_base,
            flags,
            metadata_crc32,
            key_id,
            reserved0,
            image_sha256,
            nonce,
            signature,
        ) = struct.unpack("<16I32s16s256s", payload)
        selected = "none" if selected_copy == 0xFFFFFFFF else str(selected_copy)

        return (
            f"valid={valid}, "
            f"copy_count={copy_count}, "
            f"selected_copy={selected}, "
            f"app_vector_valid={app_valid}, "
            f"magic=0x{magic:08X}, "
            f"target=0x{target_id:08X}, "
            f"board=0x{board_id:08X}, "
            f"layout=0x{flash_layout_id:08X}, "
            f"version={version}, "
            f"image_size={image_size}, "
            f"image_crc32=0x{image_crc32:08X}, "
            f"app_base=0x{app_base:08X}, "
            f"flags=0x{flags:08X}({format_bit_names(flags, BOOT_FLAG_NAMES)}), "
            f"metadata_crc32=0x{metadata_crc32:08X}, "
            f"key_id={key_id}, "
            f"image_sha256={image_sha256.hex()}, "
            f"nonce={nonce.hex()}, "
            f"signature_present={int(any(signature))}"
        )

    if len(payload) != 404:
        return f"unexpected METADATA payload length: {len(payload)}"

    (
        valid,
        copy_count,
        selected_copy,
        app_valid,
        security_state_valid,
        magic,
        target_id,
        board_id,
        flash_layout_id,
        version,
        image_size,
        image_crc32,
        app_base,
        flags,
        metadata_crc32,
        key_id,
        reserved0,
        image_sha256,
        nonce,
        signature,
        state_magic,
        state_format_version,
        state_target_id,
        state_board_id,
        state_flash_layout_id,
        rollback_floor_version,
        state_flags,
        state_crc32,
    ) = struct.unpack("<17I32s16s256s8I", payload)
    selected = "none" if selected_copy == 0xFFFFFFFF else str(selected_copy)

    return (
        f"valid={valid}, "
        f"copy_count={copy_count}, "
        f"selected_copy={selected}, "
        f"app_vector_valid={app_valid}, "
        f"security_state_valid={security_state_valid}, "
        f"magic=0x{magic:08X}, "
        f"target=0x{target_id:08X}, "
        f"board=0x{board_id:08X}, "
        f"layout=0x{flash_layout_id:08X}, "
        f"version={version}, "
        f"image_size={image_size}, "
        f"image_crc32=0x{image_crc32:08X}, "
        f"app_base=0x{app_base:08X}, "
        f"flags=0x{flags:08X}({format_bit_names(flags, BOOT_FLAG_NAMES)}), "
        f"metadata_crc32=0x{metadata_crc32:08X}, "
        f"key_id={key_id}, "
        f"image_sha256={image_sha256.hex()}, "
        f"nonce={nonce.hex()}, "
        f"signature_present={int(any(signature))}, "
        f"security_state_target=0x{state_target_id:08X}, "
        f"security_state_board=0x{state_board_id:08X}, "
        f"security_state_layout=0x{state_flash_layout_id:08X}, "
        f"rollback_floor_version={rollback_floor_version}, "
        f"security_state_flags=0x{state_flags:08X}, "
        f"security_state_crc32=0x{state_crc32:08X}"
    )


def decode_verify_image(payload: bytes) -> str:
    if len(payload) != 36:
        return f"unexpected VERIFY_IMAGE payload length: {len(payload)}"

    (
        result_error,
        metadata_valid,
        vector_valid,
        image_crc_valid,
        security_valid,
        image_size,
        expected_crc32,
        calculated_crc32,
        app_base,
    ) = struct.unpack("<9I", payload)

    return (
        f"result_error={result_error}({BOOT_ERROR_NAMES.get(result_error, 'UNKNOWN')}), "
        f"metadata_valid={metadata_valid}, "
        f"vector_valid={vector_valid}, "
        f"image_crc_valid={image_crc_valid}, "
        f"security_valid={security_valid}, "
        f"image_size={image_size}, "
        f"expected_crc32=0x{expected_crc32:08X}, "
        f"calculated_crc32=0x{calculated_crc32:08X}, "
        f"app_base=0x{app_base:08X}"
    )


def decode_diag(payload: bytes) -> str:
    if len(payload) == 52:
        (
            rx_callback_count,
            rx_byte_count,
            rx_last_len,
            rx_fifo_overflow_count,
            tx_enqueue_count,
            tx_start_count,
            tx_complete_count,
            tx_busy_reject_count,
            tx_queue_drop_count,
            tx_last_len,
            last_rx_tick_ms,
            last_tx_start_tick_ms,
            last_tx_complete_tick_ms,
        ) = struct.unpack("<13I", payload)

        return (
            f"rx_callback_count={rx_callback_count}, "
            f"rx_byte_count={rx_byte_count}, "
            f"rx_last_len={rx_last_len}, "
            f"rx_fifo_overflow_count={rx_fifo_overflow_count}, "
            f"tx_enqueue_count={tx_enqueue_count}, "
            f"tx_start_count={tx_start_count}, "
            f"tx_complete_count={tx_complete_count}, "
            f"tx_busy_reject_count={tx_busy_reject_count}, "
            f"tx_queue_drop_count={tx_queue_drop_count}, "
            f"tx_last_len={tx_last_len}, "
            f"last_rx_tick_ms={last_rx_tick_ms}, "
            f"last_tx_start_tick_ms={last_tx_start_tick_ms}, "
            f"last_tx_complete_tick_ms={last_tx_complete_tick_ms}"
        )

    if len(payload) == 72:
        (
            rx_callback_count,
            rx_byte_count,
            rx_last_len,
            rx_fifo_overflow_count,
            tx_enqueue_count,
            tx_start_count,
            tx_complete_count,
            tx_busy_reject_count,
            tx_queue_drop_count,
            tx_last_len,
            last_rx_tick_ms,
            last_tx_start_tick_ms,
            last_tx_complete_tick_ms,
            frame_ok_count,
            decode_error_count,
            last_decoded_opcode,
            last_decoded_sequence,
            last_decode_error,
        ) = struct.unpack("<18I", payload)

        return (
            f"rx_callback_count={rx_callback_count}, "
            f"rx_byte_count={rx_byte_count}, "
            f"rx_last_len={rx_last_len}, "
            f"rx_fifo_overflow_count={rx_fifo_overflow_count}, "
            f"tx_enqueue_count={tx_enqueue_count}, "
            f"tx_start_count={tx_start_count}, "
            f"tx_complete_count={tx_complete_count}, "
            f"tx_busy_reject_count={tx_busy_reject_count}, "
            f"tx_queue_drop_count={tx_queue_drop_count}, "
            f"tx_last_len={tx_last_len}, "
            f"last_rx_tick_ms={last_rx_tick_ms}, "
            f"last_tx_start_tick_ms={last_tx_start_tick_ms}, "
            f"last_tx_complete_tick_ms={last_tx_complete_tick_ms}, "
            f"frame_ok_count={frame_ok_count}, "
            f"decode_error_count={decode_error_count}, "
            f"last_decoded_opcode=0x{last_decoded_opcode:04X}, "
            f"last_decoded_sequence={last_decoded_sequence}, "
            f"last_decode_error={last_decode_error}({BOOT_ERROR_NAMES.get(last_decode_error, 'UNKNOWN')})"
        )

    return f"unexpected DIAG payload length: {len(payload)}"


def send_frame_and_read(
    port: serial.Serial,
    opcode: int,
    sequence: int,
    payload: bytes,
    retries: int,
    retry_interval: float,
    label: str | None = None,
    clear_input: bool = True,
    response_timeout: float | None = None,
) -> tuple[tuple[int, int, int, int, int, int], bytes]:
    tx_frame = build_frame(opcode, sequence, payload)
    expected_opcode = opcode | BOOT_OP_RESPONSE
    tag = label or f"opcode=0x{opcode:04X}"
    previous_timeout = port.timeout

    for attempt in range(1, retries + 1):
        if clear_input:
            port.reset_input_buffer()
        print(f"{tag} Tx[{attempt}/{retries}]: {format_hex(tx_frame)}")
        port.write(tx_frame)
        port.flush()

        try:
            if response_timeout is not None:
                port.timeout = response_timeout
            header, rx_payload, rx_frame = read_frame(port)
        except TimeoutError as exc:
            print(f"{tag} timeout on attempt {attempt}: {exc}", file=sys.stderr)
            if attempt < retries:
                time.sleep(retry_interval)
                continue
            raise
        finally:
            port.timeout = previous_timeout

        _, rx_opcode, rx_sequence, _, _, _ = header
        print(f"{tag} Rx[{attempt}/{retries}]: {format_hex(rx_frame)}")

        if rx_opcode != expected_opcode:
            if attempt < retries:
                time.sleep(retry_interval)
                continue
            raise RuntimeError(
                f"{tag} unexpected opcode 0x{rx_opcode:04X}, expected 0x{expected_opcode:04X}"
            )

        if rx_sequence != sequence:
            if attempt < retries:
                time.sleep(retry_interval)
                continue
            raise RuntimeError(
                f"{tag} unexpected sequence {rx_sequence}, expected {sequence}"
            )

        return header, rx_payload

    raise RuntimeError(f"{tag} exhausted retries")


def send_simple_app_jump(port: serial.Serial, retries: int, retry_interval: float) -> None:
    frame = build_app_command(APP_CMD_JUMP_TO_BOOTLOADER)
    ack = bytes([0xFF, APP_CMD_JUMP_TO_BOOTLOADER, 0x01, 0x01, 0x26])
    disconnect_markers = (
        "device does not recognize the command",
        "device attached to the system is not functioning",
        "clearcommerror failed",
    )

    for attempt in range(1, retries + 1):
        port.reset_input_buffer()
        print(f"app-jump Tx[{attempt}/{retries}]: {format_hex(frame)}")
        port.write(frame)
        port.flush()
        deadline = time.monotonic() + max(float(port.timeout or 0.0), 0.5)
        reply = bytearray()
        while time.monotonic() < deadline:
            try:
                chunk = port.read(32)
            except serial.SerialException as exc:
                message = str(exc).lower()
                if any(marker in message for marker in disconnect_markers):
                    print(
                        f"app-jump Rx[{attempt}/{retries}]: device disconnected during jump, assuming success"
                    )
                    return
                raise
            if chunk:
                reply.extend(chunk)
                if ack in reply:
                    print(f"app-jump Rx[{attempt}/{retries}]: {format_hex(ack)}")
                    return
                continue
            time.sleep(0.01)
        if ack in reply:
            print(f"app-jump Rx[{attempt}/{retries}]: {format_hex(ack)}")
            return
        if attempt < retries:
            time.sleep(retry_interval)
            continue
        raise RuntimeError(
            f"app-jump failed, got {format_hex(reply) if reply else 'no response'}"
        )


def best_effort_abort(port: serial.Serial, sequence: int, retries: int, retry_interval: float) -> None:
    try:
        send_frame_and_read(
            port,
            BOOT_OP_ABORT,
            sequence,
            b"",
            retries,
            retry_interval,
            label="ABORT",
        )
    except Exception:
        pass


def reconnect_after_exception(
    port: serial.Serial,
    open_retries: int,
    retry_interval: float,
) -> serial.Serial:
    port_name = port.port
    baud = port.baudrate
    timeout = port.timeout if port.timeout is not None else 1.0

    try:
        port.close()
    except Exception:
        pass

    time.sleep(retry_interval)
    reopened = open_serial_port(port_name, baud, timeout, open_retries, retry_interval)
    time.sleep(0.2)
    reopened.reset_input_buffer()
    return reopened


def target_default_write_align(target_id: int) -> int:
    return TARGET_DEFAULTS.get(target_id, {}).get("write_align", BOOT_DEFAULT_WRITE_ALIGN)


def target_default_board_id(target_id: int) -> int | None:
    value = TARGET_DEFAULTS.get(target_id, {}).get("board_id")
    return int(value) if value is not None else None


def target_default_flash_layout_id(target_id: int) -> int | None:
    value = TARGET_DEFAULTS.get(target_id, {}).get("flash_layout_id")
    return int(value) if value is not None else None


def target_default_bootloader_pid(target_id: int) -> int | None:
    value = TARGET_DEFAULTS.get(target_id, {}).get("bootloader_pid")
    return int(value) if value is not None else None


def load_image_bytes(path: pathlib.Path, pad_byte: int, write_align: int) -> tuple[bytes, int]:
    if path.suffix.lower() != ".bin":
        raise ValueError("only .bin images are supported right now")

    if write_align <= 0:
        raise ValueError("write alignment must be positive")

    image = path.read_bytes()
    if not image:
        raise ValueError("image file is empty")

    original_size = len(image)
    remainder = len(image) % write_align
    if remainder != 0:
        image += bytes([pad_byte & 0xFF]) * (write_align - remainder)

    return image, original_size


def flash_image(
    port: serial.Serial,
    image_path: pathlib.Path,
    version: int,
    flags: int,
    retries: int,
    retry_interval: float,
    open_retries: int,
    run_after: bool,
    pad_byte: int,
    write_align: int,
    begin_timeout: float,
    commit_timeout: float,
    manifest_path: pathlib.Path | None,
    manifest_key: pathlib.Path | None,
    manifest_key_id: int,
    manifest_nonce: bytes,
    manifest_out: pathlib.Path | None,
    encrypt_key: bytes | None,
    expected_board_id: int | None,
    expected_flash_layout_id: int | None,
) -> int:
    sequence = 1

    _, hello_payload = send_frame_and_read(
        port,
        BOOT_OP_HELLO,
        sequence,
        b"",
        retries,
        retry_interval,
        label="HELLO",
    )
    protocol_version, target_id, _, app_base, slot_size, max_chunk_size, _ = parse_hello_fields(hello_payload)
    sequence += 1
    if protocol_version != BOOT_PROTOCOL_VERSION:
        raise RuntimeError(
            f"protocol mismatch: device=0x{protocol_version:08X}, host=0x{BOOT_PROTOCOL_VERSION:08X}"
        )

    _, device_info_payload = send_frame_and_read(
        port,
        BOOT_OP_GET_DEVICE_INFO,
        sequence,
        b"",
        retries,
        retry_interval,
        label="DEVICE_INFO",
    )
    (
        device_protocol_version,
        info_target_id,
        board_id,
        board_revision,
        flash_layout_id,
        _flash_base,
        _flash_size,
        info_app_base,
        _metadata_base,
        _metadata_size,
        _erase_size,
        info_write_size,
        info_max_chunk_size,
        _capabilities,
    ) = parse_device_info_fields(device_info_payload)
    sequence += 1

    if device_protocol_version != protocol_version:
        raise RuntimeError(
            f"device info protocol mismatch: hello=0x{protocol_version:08X}, "
            f"device-info=0x{device_protocol_version:08X}"
        )
    if info_target_id != target_id:
        raise RuntimeError(
            f"device info target mismatch: hello=0x{target_id:08X}, "
            f"device-info=0x{info_target_id:08X}"
        )
    if info_app_base != app_base:
        raise RuntimeError(
            f"device info app base mismatch: hello=0x{app_base:08X}, "
            f"device-info=0x{info_app_base:08X}"
        )
    if info_max_chunk_size != max_chunk_size:
        raise RuntimeError(
            f"device info max chunk mismatch: hello={max_chunk_size}, "
            f"device-info={info_max_chunk_size}"
        )
    if expected_board_id is not None and expected_board_id != board_id:
        raise RuntimeError(
            f"device board mismatch: expected=0x{expected_board_id:08X}, "
            f"device=0x{board_id:08X}"
        )
    if expected_flash_layout_id is not None and expected_flash_layout_id != flash_layout_id:
        raise RuntimeError(
            f"device flash layout mismatch: expected=0x{expected_flash_layout_id:08X}, "
            f"device=0x{flash_layout_id:08X}"
        )

    if write_align == 0:
        write_align = info_write_size or target_default_write_align(target_id)
    image, original_size = load_image_bytes(image_path, pad_byte, write_align)
    if manifest_key is not None:
        flags |= BOOT_FLAG_VERIFY_SIGNATURE
    if (flags & BOOT_FLAG_ENCRYPTED_IMAGE) != 0:
        flags |= BOOT_FLAG_VERIFY_SIGNATURE
        if manifest_nonce == bytes(16) and manifest_path is None:
            manifest_nonce = secrets.token_bytes(16)
    if (flags & BOOT_FLAG_ANTI_ROLLBACK) != 0:
        flags |= BOOT_FLAG_VERIFY_SIGNATURE

    if len(image) > slot_size:
        raise RuntimeError(
            f"image too large: padded_size={len(image)} slot_size={slot_size}"
        )

    usable_chunk = min(max_chunk_size, BOOT_FRAME_MAX_PAYLOAD - BOOT_DATA_PREFIX_SIZE)
    usable_chunk -= usable_chunk % write_align
    if usable_chunk == 0:
        raise RuntimeError("device reported unusable max_chunk_size")

    image_crc = crc32(image)
    manifest_payload: bytes | None = None
    upload_image = image
    if manifest_path is not None:
        manifest_payload = manifest_path.read_bytes()
        manifest_info = parse_manifest_header(manifest_payload)
        if manifest_info["magic"] != BOOT_MANIFEST_MAGIC:
            raise RuntimeError("manifest magic mismatch")
        if manifest_info["manifest_version"] != BOOT_MANIFEST_VERSION:
            raise RuntimeError("manifest version mismatch")
        if manifest_info["target_id"] != target_id:
            raise RuntimeError(
                f"manifest target mismatch: manifest=0x{manifest_info['target_id']:08X}, "
                f"device=0x{target_id:08X}"
            )
        if manifest_info["board_id"] != board_id:
            raise RuntimeError(
                f"manifest board mismatch: manifest=0x{manifest_info['board_id']:08X}, "
                f"device=0x{board_id:08X}"
            )
        if manifest_info["flash_layout_id"] != flash_layout_id:
            raise RuntimeError(
                f"manifest flash layout mismatch: manifest=0x{manifest_info['flash_layout_id']:08X}, "
                f"device=0x{flash_layout_id:08X}"
            )
        if manifest_info["image_size"] != len(image):
            raise RuntimeError(
                f"manifest image size mismatch: manifest={manifest_info['image_size']}, "
                f"image={len(image)}"
            )
        if manifest_info["image_crc32"] != image_crc:
            raise RuntimeError(
                f"manifest image CRC mismatch: manifest=0x{manifest_info['image_crc32']:08X}, "
                f"image=0x{image_crc:08X}"
            )
        if manifest_info["firmware_version"] != version:
            raise RuntimeError(
                f"manifest version mismatch: manifest={manifest_info['firmware_version']}, "
                f"requested={version}"
            )
        manifest_flags = manifest_info["flags"]
        if (manifest_flags & BOOT_FLAG_VERIFY_SIGNATURE) == 0:
            raise RuntimeError("manifest must set BOOT_FLAG_VERIFY_SIGNATURE")
        if flags not in (0, manifest_flags):
            raise RuntimeError(
                f"manifest flags mismatch: manifest=0x{manifest_flags:08X}, requested=0x{flags:08X}"
            )
        flags = manifest_flags
        manifest_nonce = manifest_payload[BOOT_MANIFEST_NONCE_OFFSET:BOOT_MANIFEST_NONCE_OFFSET + 16]
    elif manifest_key is not None:
        manifest_payload = build_signed_manifest(
            target_id,
            board_id,
            flash_layout_id,
            image,
            image_crc,
            version,
            flags,
            manifest_key_id,
            manifest_nonce,
            manifest_key,
        )
        if manifest_out is not None:
            manifest_out.write_bytes(manifest_payload)

    if (flags & BOOT_FLAG_ENCRYPTED_IMAGE) != 0:
        if manifest_payload is None:
            raise RuntimeError("--manifest or --manifest-key is required for encrypted updates")
        upload_image = aes128_ctr_crypt(image, encrypt_key or BOOT_DEV_AES128_KEY, manifest_nonce)

    print(
        f"image={image_path.name}, original_size={original_size}, padded_size={len(image)}, "
        f"crc32=0x{image_crc:08X}, app_base=0x{app_base:08X}, "
        f"target=0x{target_id:08X}, board=0x{board_id:08X}, "
        f"board_revision={board_revision}, layout=0x{flash_layout_id:08X}, "
        f"write_align={write_align}, chunk={usable_chunk}"
    )

    begin_payload = struct.pack(
        "<7I",
        target_id,
        board_id,
        flash_layout_id,
        len(image),
        image_crc,
        version,
        flags,
    )
    try:
        _, begin_rsp = send_frame_and_read(
            port,
            BOOT_OP_BEGIN,
            sequence,
            begin_payload,
            retries,
            retry_interval,
            label="BEGIN",
            response_timeout=begin_timeout,
        )
    except (TimeoutError, serial.SerialException, OSError) as exc:
        print(f"BEGIN response unavailable: {exc}; reconnecting to verify state", file=sys.stderr)
        port = reconnect_after_exception(port, open_retries, retry_interval)
        _, status_payload = send_frame_and_read(
            port,
            BOOT_OP_GET_STATUS,
            0x7FFE,
            b"",
            retries,
            retry_interval,
            label="STATUS-RECOVER",
        )
        status, last_error, written_size, expected_size, _, _, _, _ = parse_status_fields(status_payload)
        print(decode_status(status_payload))
        if status != 2 or last_error != 0 or written_size != 0 or expected_size != len(image):
            raise RuntimeError("BEGIN failed and bootloader did not enter RECEIVING state") from exc
        begin_rsp = status_payload
    sequence += 1
    status, last_error, _, expected_size, _, _, _, _ = parse_status_fields(begin_rsp)
    if last_error != 0:
        raise RuntimeError(f"BEGIN failed: {decode_status(begin_rsp)}")
    print(decode_status(begin_rsp))

    if (flags & BOOT_FLAG_VERIFY_SIGNATURE) != 0:
        if manifest_payload is None:
            raise RuntimeError("--manifest or --manifest-key is required when signature verification is enabled")
        _, manifest_rsp = send_frame_and_read(
            port,
            BOOT_OP_SET_MANIFEST,
            sequence,
            manifest_payload,
            retries,
            retry_interval,
            label="SET_MANIFEST",
        )
        sequence += 1
        status, last_error, _, _, _, _, _, _ = parse_status_fields(manifest_rsp)
        if last_error != 0:
            raise RuntimeError(f"SET_MANIFEST failed: {decode_status(manifest_rsp)}")
        print(decode_status(manifest_rsp))

    total_chunks = (len(upload_image) + usable_chunk - 1) // usable_chunk
    for index, offset in enumerate(range(0, len(upload_image), usable_chunk), start=1):
        chunk = upload_image[offset: offset + usable_chunk]
        payload = struct.pack("<II", offset, len(chunk)) + chunk
        _, data_rsp = send_frame_and_read(
            port,
            BOOT_OP_DATA,
            sequence,
            payload,
            retries,
            retry_interval,
            label=f"DATA {index}/{total_chunks}",
        )
        sequence += 1

        status, last_error, written_size, _, running_crc32, _, _, _ = parse_status_fields(data_rsp)
        if last_error != 0:
            raise RuntimeError(
                f"DATA failed at offset 0x{offset:08X}: {decode_status(data_rsp)}"
            )
        print(
            f"progress={written_size}/{expected_size} "
            f"({written_size * 100 // expected_size}%), running_crc32=0x{running_crc32:08X}"
        )

    commit_payload = struct.pack("<2I", len(image), image_crc)
    try:
        _, commit_rsp = send_frame_and_read(
            port,
            BOOT_OP_COMMIT,
            sequence,
            commit_payload,
            retries,
            retry_interval,
            label="COMMIT",
            response_timeout=commit_timeout,
        )
    except (TimeoutError, serial.SerialException, OSError) as exc:
        print(f"COMMIT response unavailable: {exc}; reconnecting to verify state", file=sys.stderr)
        port = reconnect_after_exception(port, open_retries, retry_interval)
        _, status_payload = send_frame_and_read(
            port,
            BOOT_OP_GET_STATUS,
            0x7FFD,
            b"",
            retries,
            retry_interval,
            label="STATUS-COMMIT-RECOVER",
        )
        status, last_error, written_size, expected_size, running_crc32, _, _, _ = parse_status_fields(status_payload)
        print(decode_status(status_payload))
        if (
            status != 3
            or last_error != 0
            or written_size != len(image)
            or expected_size != len(image)
            or running_crc32 != image_crc
        ):
            raise RuntimeError("COMMIT failed and bootloader did not report COMMITTED state") from exc
        commit_rsp = status_payload
    sequence += 1
    status, last_error, written_size, expected_size, running_crc32, _, _, _ = parse_status_fields(commit_rsp)
    if last_error != 0 or status != 3:
        raise RuntimeError(f"COMMIT failed: {decode_status(commit_rsp)}")
    print(decode_status(commit_rsp))

    if run_after:
        try:
            _, boot_rsp = send_frame_and_read(
                port,
                BOOT_OP_BOOT_APP,
                sequence,
                b"",
                retries,
                retry_interval,
                label="BOOT_APP",
            )
            print(decode_status(boot_rsp))
        except (TimeoutError, serial.SerialException, OSError) as exc:
            print(f"BOOT_APP jump requested; device disconnected/re-enumerating: {exc}", file=sys.stderr)

    return 0


def send_command(
    port: serial.Serial | None,
    opcode: int,
    sequence: int,
    payload: bytes,
    dry_run: bool,
    retries: int,
    retry_interval: float,
) -> int:
    tx_frame = build_frame(opcode, sequence, payload)

    if dry_run:
        print(f"Tx: {format_hex(tx_frame)}")
        return 0

    assert port is not None
    expected_opcode = opcode | BOOT_OP_RESPONSE

    for attempt in range(1, retries + 1):
        port.reset_input_buffer()
        print(f"Tx[{attempt}/{retries}]: {format_hex(tx_frame)}")
        port.write(tx_frame)
        port.flush()

        try:
            header, rx_payload, rx_frame = read_frame(port)
        except TimeoutError as exc:
            print(f"timeout on attempt {attempt}: {exc}", file=sys.stderr)
            if attempt < retries:
                time.sleep(retry_interval)
                continue
            return 1

        _, rx_opcode, rx_sequence, _, _, _ = header
        print(f"Rx[{attempt}/{retries}]: {format_hex(rx_frame)}")
        print(f"opcode=0x{rx_opcode:04X}, sequence={rx_sequence}")

        if rx_opcode != expected_opcode:
            print(f"unexpected opcode: expected 0x{expected_opcode:04X}", file=sys.stderr)
            if attempt < retries:
                time.sleep(retry_interval)
                continue
            return 1

        if opcode == BOOT_OP_HELLO:
            print(decode_hello(rx_payload))
        elif opcode == BOOT_OP_GET_DEVICE_INFO:
            print(decode_device_info(rx_payload))
        elif opcode == BOOT_OP_GET_STATUS:
            print(decode_status(rx_payload))
        elif opcode == BOOT_OP_GET_DIAG:
            print(decode_diag(rx_payload))
        elif opcode == BOOT_OP_GET_METADATA:
            print(decode_metadata(rx_payload))
        elif opcode == BOOT_OP_VERIFY_IMAGE:
            print(decode_verify_image(rx_payload))
        elif opcode in (BOOT_OP_CLEAR_METADATA, BOOT_OP_REBOOT, BOOT_OP_BOOT_APP, BOOT_OP_SET_MANIFEST):
            print(decode_status(rx_payload))
        else:
            print(f"payload_len={len(rx_payload)}")

        return 0

    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description="Affine USB-CDC bootloader smoke test tool")
    parser.add_argument("port", help="serial port, for example COM5")
    parser.add_argument(
        "command",
        choices=[
            "hello",
            "status",
            "device-info",
            "diag",
            "metadata",
            "clear-metadata",
            "reboot",
            "verify-image",
            "probe",
            "flash",
            "app-jump",
        ],
        help="command to send",
    )
    parser.add_argument("image", nargs="?", help="application .bin image path for flash")
    parser.add_argument("--baud", type=int, default=115200, help="serial baud rate placeholder")
    parser.add_argument("--timeout", type=float, default=1.0, help="read timeout in seconds")
    parser.add_argument("--interval", type=float, default=0.1, help="delay between probe commands")
    parser.add_argument("--retries", type=int, default=1, help="retry count for each command")
    parser.add_argument("--retry-interval", type=float, default=0.1, help="delay between retries")
    parser.add_argument("--open-retries", type=int, default=10, help="serial open retry count")
    parser.add_argument("--version", type=lambda value: int(value, 0), default=1, help="firmware version for BEGIN")
    parser.add_argument("--target-id", type=lambda value: int(value, 0), help="target ID for offline manifest dry-run generation")
    parser.add_argument("--board-id", type=lambda value: int(value, 0), help="expected or offline board ID")
    parser.add_argument("--flash-layout-id", type=lambda value: int(value, 0), help="expected or offline flash layout ID")
    parser.add_argument("--flags", type=lambda value: int(value, 0), default=0, help="security flags for BEGIN")
    parser.add_argument("--manifest", type=pathlib.Path, help="prebuilt signed manifest to send before DATA")
    parser.add_argument("--manifest-key", type=pathlib.Path, help="RSA-2048 private key used to sign a manifest")
    parser.add_argument("--manifest-out", type=pathlib.Path, help="write the generated manifest to this path")
    parser.add_argument("--manifest-key-id", type=lambda value: int(value, 0), default=0, help="manifest signing key identifier")
    parser.add_argument(
        "--manifest-nonce",
        type=lambda value: parse_hex_bytes(value, 16),
        default=bytes(16),
        help="16-byte manifest encryption nonce as hex",
    )
    parser.add_argument(
        "--encrypted",
        action="store_true",
        help="encrypt the transmitted stream with AES-128 CTR and require a signed manifest",
    )
    parser.add_argument(
        "--anti-rollback",
        action="store_true",
        help="request anti-rollback for a signed manifest update",
    )
    parser.add_argument(
        "--encrypt-key",
        type=lambda value: parse_hex_bytes(value, 16),
        help="16-byte AES-128 CTR key as hex; defaults to the development key embedded in boot_keys.h",
    )
    parser.add_argument("--pad-byte", type=lambda value: int(value, 0), default=0xFF, help="padding byte for flash alignment")
    parser.add_argument("--write-align", type=lambda value: int(value, 0), default=0, help="override flash write alignment; 0 auto-detects known targets")
    parser.add_argument("--begin-timeout", type=float, default=6.0, help="response timeout for BEGIN in seconds")
    parser.add_argument("--commit-timeout", type=float, default=3.0, help="response timeout for COMMIT in seconds")
    parser.add_argument("--run", action="store_true", help="boot the application after COMMIT")
    parser.add_argument("--app-jump", action="store_true", help="send Mai app command 0x25 to reboot into bootloader before flashing")
    parser.add_argument("--reconnect-delay", type=float, default=1.2, help="delay after app-jump reset before reopening the bootloader port")
    parser.add_argument("--wait-timeout", type=float, default=8.0, help="USB re-enumeration wait timeout in seconds")
    parser.add_argument("--usb-serial", help="USB serial number to match while waiting for re-enumeration")
    parser.add_argument("--usb-vid", type=parse_int_auto, default=DEFAULT_USB_VID, help="USB VID used for wait matching")
    parser.add_argument(
        "--bootloader-pid",
        type=parse_int_auto,
        help="bootloader USB PID used for wait matching; defaults from --target-id or G431",
    )
    parser.add_argument("--app-pid", type=parse_int_auto, default=DEFAULT_APP_PID, help="application USB PID used for wait matching")
    parser.add_argument(
        "--no-wait-bootloader",
        dest="wait_bootloader",
        action="store_false",
        default=True,
        help="do not wait for the bootloader COM port after app-jump",
    )
    parser.add_argument("--wait-app", action="store_true", help="wait for the application COM port after --run")
    parser.add_argument("--dry-run", action="store_true", help="build and print frames without opening the port")
    args = parser.parse_args()

    if args.bootloader_pid is None:
        args.bootloader_pid = target_default_bootloader_pid(args.target_id or 0) or DEFAULT_G431_BOOTLOADER_PID

    commands = []
    if args.command == "hello":
        commands.append((BOOT_OP_HELLO, 1, b""))
    elif args.command == "status":
        commands.append((BOOT_OP_GET_STATUS, 2, b""))
    elif args.command == "device-info":
        commands.append((BOOT_OP_GET_DEVICE_INFO, 2, b""))
    elif args.command == "diag":
        commands.append((BOOT_OP_GET_DIAG, 3, b""))
    elif args.command == "metadata":
        commands.append((BOOT_OP_GET_METADATA, 4, b""))
    elif args.command == "clear-metadata":
        commands.append((BOOT_OP_CLEAR_METADATA, 5, b""))
    elif args.command == "reboot":
        commands.append((BOOT_OP_REBOOT, 6, b""))
    elif args.command == "verify-image":
        commands.append((BOOT_OP_VERIFY_IMAGE, 7, b""))
    elif args.command == "probe":
        commands.append((BOOT_OP_HELLO, 1, b""))
        commands.append((BOOT_OP_GET_DEVICE_INFO, 2, b""))
        commands.append((BOOT_OP_GET_STATUS, 3, b""))
        commands.append((BOOT_OP_GET_METADATA, 4, b""))
    elif args.command == "app-jump":
        pass
    else:
        if not args.image:
            parser.error("flash command requires an application .bin path")

    if args.dry_run:
        if args.command == "flash":
            image_path = pathlib.Path(args.image)
            write_align = args.write_align or target_default_write_align(args.target_id or 0)
            image, original_size = load_image_bytes(image_path, args.pad_byte, write_align)
            dry_flags = args.flags | (BOOT_FLAG_VERIFY_SIGNATURE if args.manifest_key else 0)
            if args.manifest:
                manifest_info = parse_manifest_header(args.manifest.read_bytes())
                dry_flags = manifest_info["flags"]
            if args.encrypted:
                dry_flags |= BOOT_FLAG_ENCRYPTED_IMAGE
                dry_flags |= BOOT_FLAG_VERIFY_SIGNATURE
                if args.manifest_nonce == bytes(16):
                    args.manifest_nonce = secrets.token_bytes(16)
            if args.anti_rollback:
                dry_flags |= BOOT_FLAG_ANTI_ROLLBACK
                dry_flags |= BOOT_FLAG_VERIFY_SIGNATURE
            if args.manifest_key and args.manifest_out:
                if args.target_id is None:
                    parser.error("--target-id is required with --dry-run --manifest-key --manifest-out")
                board_id = args.board_id
                if board_id is None:
                    board_id = target_default_board_id(args.target_id)
                if board_id is None:
                    parser.error("--board-id is required for unknown --target-id")
                flash_layout_id = args.flash_layout_id
                if flash_layout_id is None:
                    flash_layout_id = target_default_flash_layout_id(args.target_id)
                if flash_layout_id is None:
                    parser.error("--flash-layout-id is required for unknown --target-id")
                manifest = build_signed_manifest(
                    args.target_id,
                    board_id,
                    flash_layout_id,
                    image,
                    crc32(image),
                    args.version,
                    dry_flags,
                    args.manifest_key_id,
                    args.manifest_nonce,
                    args.manifest_key,
                )
                args.manifest_out.write_bytes(manifest)
            print(
                f"flash image={image_path}, original_size={original_size}, "
                f"padded_size={len(image)}, write_align={write_align}, "
                f"crc32=0x{crc32(image):08X}, flags=0x{dry_flags:08X}, "
                f"target=0x{args.target_id or 0:08X}, "
                f"board=0x{(args.board_id if args.board_id is not None else target_default_board_id(args.target_id or 0) or 0):08X}, "
                f"layout=0x{(args.flash_layout_id if args.flash_layout_id is not None else target_default_flash_layout_id(args.target_id or 0) or 0):08X}, "
                f"sha256={hashlib.sha256(image).hexdigest()}"
            )
            return 0
        if args.command == "app-jump":
            print(f"Tx: {format_hex(build_app_command(APP_CMD_JUMP_TO_BOOTLOADER))}")
            return 0
        for opcode, sequence, payload in commands:
            result = send_command(
                None,
                opcode,
                sequence,
                payload,
                True,
                args.retries,
                args.retry_interval,
            )
            if result != 0:
                return result
        return 0

    device_serial = resolve_wait_serial(args.port, args.usb_serial)

    if args.command == "flash" and args.app_jump:
        with open_serial_port(
            args.port,
            args.baud,
            args.timeout,
            args.open_retries,
            args.retry_interval,
        ) as port:
            time.sleep(0.2)
            send_simple_app_jump(port, args.retries, args.retry_interval)
        if args.wait_bootloader:
            bootloader_port = wait_for_usb_port(
                args.usb_vid,
                args.bootloader_pid,
                device_serial,
                args.wait_timeout,
                "bootloader",
            )
            args.port = bootloader_port.device
        else:
            time.sleep(args.reconnect_delay)

    with open_serial_port(
        args.port,
        args.baud,
        args.timeout,
        args.open_retries,
        args.retry_interval,
    ) as port:
        time.sleep(0.2)
        port.reset_input_buffer()

        if args.command == "app-jump":
            send_simple_app_jump(port, args.retries, args.retry_interval)
            if args.wait_bootloader:
                port.close()
                wait_for_usb_port(
                    args.usb_vid,
                    args.bootloader_pid,
                    device_serial,
                    args.wait_timeout,
                    "bootloader",
                )
            return 0

        if args.command == "flash":
            flash_flags = args.flags
            if args.encrypted:
                flash_flags |= BOOT_FLAG_ENCRYPTED_IMAGE
            if args.anti_rollback:
                flash_flags |= BOOT_FLAG_ANTI_ROLLBACK
            try:
                result = flash_image(
                    port,
                    pathlib.Path(args.image),
                    args.version,
                    flash_flags,
                    args.retries,
                    args.retry_interval,
                    args.open_retries,
                    args.run,
                    args.pad_byte,
                    args.write_align,
                    args.begin_timeout,
                    args.commit_timeout,
                    args.manifest,
                    args.manifest_key,
                    args.manifest_key_id,
                    args.manifest_nonce,
                    args.manifest_out,
                    args.encrypt_key,
                    args.board_id,
                    args.flash_layout_id,
                )
                if result == 0 and args.run and args.wait_app:
                    port.close()
                    wait_for_usb_port(
                        args.usb_vid,
                        args.app_pid,
                        device_serial,
                        args.wait_timeout,
                        "application",
                    )
                return result
            except Exception as exc:
                best_effort_abort(port, 0x7FFF, args.retries, args.retry_interval)
                print(f"flash failed: {exc}", file=sys.stderr)
                return 1

        for index, (opcode, sequence, payload) in enumerate(commands):
            result = send_command(
                port,
                opcode,
                sequence,
                payload,
                False,
                args.retries,
                args.retry_interval,
            )
            if result != 0:
                return result

            if opcode == BOOT_OP_REBOOT and args.wait_bootloader:
                port.close()
                time.sleep(args.reconnect_delay)
                wait_for_usb_port(
                    args.usb_vid,
                    args.bootloader_pid,
                    device_serial,
                    args.wait_timeout,
                    "bootloader",
                )
                return 0

            if index + 1 < len(commands):
                time.sleep(args.interval)

    return 0


if __name__ == "__main__":
    sys.exit(main())
