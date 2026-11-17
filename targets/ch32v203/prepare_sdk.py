"""Adapt selected pinned SDK files into the build tree; SDK stays read-only."""
from pathlib import Path
import sys

sdk, out, variant = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
out.mkdir(parents=True, exist_ok=True)


def replace(source, old, new, count=1):
    actual = source.count(old)
    if actual != count:
        raise SystemExit(f"SDK adaptation mismatch: {old!r}: {actual} != {count}")
    return source.replace(old, new)


def read(path):
    return (sdk / path).read_text(encoding="utf-8")


def write(name, text):
    (out / name).write_text(text, encoding="utf-8", newline="\n")


startup = read('EVT/EXAM/SRC/Startup/startup_ch32v20x_D6.S')
# Software ISR frames for upstream GCC; disable WCH hardware stack/nesting.
startup = replace(startup, 'li t0, 0x3', 'li t0, 0x0')
# The SDK's mstatus 0x88 leaves MPP = U, so main would run in User mode, where
# the jump's mtvec write and the application startup's CSR writes trap (the SDK
# IAP example instead jumps from a Machine-mode interrupt handler). MPP = M keeps
# the bootloader in Machine mode; the application startup picks its own mode.
startup = replace(startup, 'li t0, 0x88', 'li t0, 0x1888')
write('startup.S', startup)
base = 'EVT/EXAM/USB/USBD/SimulateCDC/User/USBLIB/'
if variant == 'usbd':
    for name in ('usb_conf.h', 'usb_prop.h', 'usb_pwr.h', 'usb_istr.h', 'hw_config.h'):
        text = read(base + 'CONFIG/' + name)
        if name == 'usb_conf.h':
            text = replace(text, '(15)', '(4)')
        write(name, text)
    for name in ('usb_pwr.c', 'usb_istr.c'):
        write(name, read(base + 'CONFIG/' + name))
    text = read(base + 'CONFIG/usb_prop.c')
    text = replace(text, 'void USBD_Reset(void)\n{',
                   'void USBD_Reset(void)\n{\n  boot_ch32_usb_reset();')
    text = replace(text,
        '  uint32_t Request_No = pInformation->USBbRequest;\n  uint8_t *(*CopyRoutine)(uint16_t);',
        '  if (pInformation->USBwIndex != 0 || pInformation->USBwLength != 7 ||\n'
        '      pInformation->USBwValue != 0 ||\n'
        '      (pInformation->USBbmRequestType != 0xA1 && pInformation->USBbmRequestType != 0x21) ||\n'
        '      ((RequestNo == CDC_GET_LINE_CODING) != (pInformation->USBbmRequestType == 0xA1))) return USB_UNSUPPORT;\n'
        '  uint32_t Request_No = pInformation->USBbRequest;\n  uint8_t *(*CopyRoutine)(uint16_t);')
    text = replace(text, '  if (pInfo->Current_Configuration != 0)\n  {\n    bDeviceState = CONFIGURED;\n  }',
                   '  bDeviceState = pInfo->Current_Configuration ? CONFIGURED : ADDRESSED;\n'
                   '  if (!pInfo->Current_Configuration) boot_ch32_usb_reset();')
    text = replace(text, 'RESULT USBD_NoData_Setup(uint8_t RequestNo)\n{',
                   'RESULT USBD_NoData_Setup(uint8_t RequestNo)\n{\n'
                   '  if (pInformation->USBbmRequestType != 0x21 || pInformation->USBwIndex != 0 ||\n'
                   '      pInformation->USBwLength != 0) return USB_UNSUPPORT;')
    write('usb_prop.c', text)
else:
    base = 'EVT/EXAM/USB/USBFS/DEVICE/SimulateCDC/User/USB_Device/'
    write('ch32v20x_usbfs_device.h', read(base + 'ch32v20x_usbfs_device.h'))
    text = read(base + 'ch32v20x_usbfs_device.c')
    text = replace(text, 'interrupt("WCH-Interrupt-fast")', 'interrupt("machine")')
    text = replace(text, '(uint32_t)UART2_Tx_Buf', '(uint32_t)USBFS_EP2_Buf')
    text = replace(text, '    uint32_t baudrate;', '')
    text = replace(text, 'Uart.USB_Up_IngFlag = 0x00;', 'boot_ch32_usb_tx_done();')
    start = text.index('                                      /* Save relevant parameters')
    end = text.index('\n                                 }', start)
    text = text[:start] + '''                                      if(len==7U && USBFS_SetupReqType==0x21U)
                                          memcpy(Uart.Com_Cfg,USBFS_EP0_Buf,7U);
                                      else { USBFSD->UEP0_TX_CTRL=USBFS_UEP_T_RES_STALL; break; }
''' + text[end:]
    start = text.index('                        USBFSD->UEP2_RX_CTRL ^=')
    end = text.index('\n                        break;', start)
    text = text[:start] + '''                        if(intst & USBFS_UIS_TOG_OK) {
                            USBFSD->UEP2_RX_CTRL ^= USBFS_UEP_R_TOG;
                            USBFSD->UEP2_RX_CTRL=(USBFSD->UEP2_RX_CTRL & ~USBFS_UEP_R_RES_MASK)|USBFS_UEP_R_RES_NAK;
                            boot_ch32_usb_rx(USBFS_EP2_Buf,USBFSD->RX_LEN);
                        }''' + text[end:]
    start = text.index('                    /* usb non-standard request processing */')
    end = text.index('\n                }\n                else', start)
    text = text[:start] + '''                    /* CDC interface 0 only; bounded control buffers. */
                    if(USBFS_SetupReqIndex!=0 ||
                       ((USBFS_SetupReqCode==CDC_GET_LINE_CODING || USBFS_SetupReqCode==CDC_SET_LINE_CODING) &&
                        (USBFS_SetupReqLen!=7 || USBFS_SetupReqValue!=0))) errflag=0xff;
                    else if(USBFS_SetupReqCode==CDC_GET_LINE_CODING && USBFS_SetupReqType==0xA1) {
                        memcpy(USBFS_EP0_Buf,Uart.Com_Cfg,7); len=7;
                    } else if(USBFS_SetupReqCode==CDC_SET_LINE_CODING && USBFS_SetupReqType==0x21) {
                        len=0;
                    } else if((USBFS_SetupReqCode==CDC_SET_LINE_CTLSTE || USBFS_SetupReqCode==CDC_SEND_BREAK) &&
                              USBFS_SetupReqType==0x21 && USBFS_SetupReqLen==0) len=0;
                    else errflag=0xff;''' + text[end:]
    text = replace(text, '        UART2_ParaInit( 1 );', '        boot_ch32_usb_reset();')
    text = replace(text,
        '                            USBFS_DevConfig = (uint8_t)( USBFS_SetupReqValue & 0xFF );\n'
        '                            USBFS_DevEnumStatus = 0x01;',
        '                            if(USBFS_SetupReqValue>1 || USBFS_SetupReqLen || USBFS_SetupReqIndex) errflag=0xff;\n'
        '                            else {\n'
        '                                USBFS_DevConfig=(uint8_t)USBFS_SetupReqValue;\n'
        '                                USBFS_DevEnumStatus=USBFS_DevConfig;\n'
        '                                if(!USBFS_DevConfig) boot_ch32_usb_reset();\n'
        '                            }')
    write('ch32v20x_usbfs_device.c', text)
