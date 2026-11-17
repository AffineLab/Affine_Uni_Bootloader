# Third Party Notices

This repository includes selected third-party source files required to build the STM32F072, STM32G431, and STM32H503 bootloader targets. The project license in `LICENSE` applies to Affine-authored code only. Third-party files retain their original copyright notices and license terms.

## Vendored Components

| Path | Component | Copyright holder | License |
| --- | --- | --- | --- |
| `third_party/stm32cube_f0/Drivers/CMSIS/Include` | CMSIS Core | Arm Limited | Apache License 2.0 |
| `third_party/stm32cube_f0/Drivers/CMSIS/Device/ST/STM32F0xx/Include` | STM32F0 CMSIS Device | Arm Limited and STMicroelectronics | Apache License 2.0 |
| `third_party/stm32cube_f0/Drivers/STM32F0xx_HAL_Driver` | STM32F0 HAL/LL drivers | STMicroelectronics | BSD 3-Clause |
| `third_party/stm32cube_f0/Core` | STM32F0 HAL configuration, system file, and startup file | STMicroelectronics | As stated in the source file headers |
| `third_party/stm32cube_f0/Middlewares/Third_Party/AL94_USB_Composite/COMPOSITE` | STM32 USB Device Library / Composite USB middleware subset | STMicroelectronics | SLA0044, as stated in the source file headers |
| `third_party/stm32cube_g4/Drivers/CMSIS/Include` | CMSIS Core | Arm Limited | Apache License 2.0 |
| `third_party/stm32cube_g4/Drivers/CMSIS/Device/ST/STM32G4xx/Include` | STM32G4 CMSIS Device | Arm Limited and STMicroelectronics | Apache License 2.0 |
| `third_party/stm32cube_g4/Drivers/STM32G4xx_HAL_Driver` | STM32G4 HAL/LL drivers | STMicroelectronics | BSD 3-Clause |
| `third_party/stm32cube_g4/Core` | STM32G4 HAL configuration, system file, and startup file | STMicroelectronics | As stated in the source file headers |
| `third_party/stm32cube_g4/Middlewares/Third_Party/AL94_USB_Composite/COMPOSITE` | STM32 USB Device Library / Composite USB middleware subset | STMicroelectronics | SLA0044, as stated in the source file headers |
| `third_party/stm32cube_h5/Drivers/CMSIS/Include` | CMSIS Core | Arm Limited | Apache License 2.0 |
| `third_party/stm32cube_h5/Drivers/CMSIS/Device/ST/STM32H5xx/Include` | STM32H5 CMSIS Device | Arm Limited and STMicroelectronics | Apache License 2.0 |
| `third_party/stm32cube_h5/Drivers/STM32H5xx_HAL_Driver` | STM32H5 HAL/LL drivers | STMicroelectronics | BSD 3-Clause |
| `third_party/stm32cube_h5/Core` | STM32H5 HAL configuration, system file, and startup file | STMicroelectronics | As stated in the source file headers |
| `third_party/stm32cube_h5/Middlewares/Third_Party/AL94_USB_Composite/COMPOSITE` | STM32 USB Device Library / Composite USB middleware subset | STMicroelectronics | SLA0044, as stated in the source file headers |

STM32CubeF0 source package: https://github.com/STMicroelectronics/STM32CubeF0

STM32CubeF0 package license metadata: `third_party/stm32cube_f0/LICENSE.md`

STM32CubeG4 source package: https://github.com/STMicroelectronics/STM32CubeG4

STM32CubeG4 package license metadata: `third_party/stm32cube_g4/LICENSE.md`

STM32CubeH5 source package: https://github.com/STMicroelectronics/STM32CubeH5

STM32CubeH5 package license metadata: `third_party/stm32cube_h5/LICENSE.md`

SLA0044 notice URL used by the vendored USB middleware headers: https://www.st.com/SLA0044

## External Artery dependency (not vendored)

The AT32F405 build uses [ArteryTek/AT32F402_405_Firmware_Library](https://github.com/ArteryTek/AT32F402_405_Firmware_Library), v2.1.5 at `5dd9d55a2ce9ffa8fe0cb2652ac142920f2094a3`, selected with `AFFINE_AT32_SDK_DIR`. No Artery SDK files are distributed in this source repository. CI fetches the dependency into its build directory.

The SDK's top-level `LICENSE` identifies BSD 3-Clause (Copyright (c) 2022, Artery-MCU). Its source headers carry a separate BSP notice (Copyright (c) 2025, Artery Technology), authorizing use, copying and distribution for design and development in conjunction with Artery microcontrollers, with an as-is warranty disclaimer. Those terms remain applicable to the external SDK and binaries incorporating it; the SDK is not relicensed as Apache-2.0. Retain the SDK license and notices when redistributing such binaries. The BSD conditions and disclaimer are reproduced below; consult the pinned SDK's source headers for the BSP notice.

## External Raspberry Pi dependencies (not vendored)

The RP2350 build uses pico-sdk 2.3.1 at `079c6f39023649b154152db30f1d781e884879bc`
and TinyUSB at `86ad6e56c1700e85f1c5678607a762cfe3aa2f47`; picotool 2.3.1 at
`2041936441b48a3cc53ae3da9e805229fe8f4e18` generates UF2 output. No sources from
these projects are copied here. pico-sdk's top-level license is BSD 3-Clause;
TinyUSB is MIT. picotool and SDK components retain their own source notices.
When redistributing firmware/tools, retain the applicable external license texts
and notices, including those of SDK components actually linked. See
`targets/rp2350/PORTING.md` for configuration and pinned-fetch details.

## BSD 3-Clause License Text

The BSD 3-Clause conditions and disclaimer below apply to components identified above under that license; their respective copyright notices remain applicable.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES, INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT, INCLUDING NEGLIGENCE OR OTHERWISE ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## ESP-IDF and esp_tinyusb (external)

The ESP32-S3 agent and test app build against ESP-IDF v6.1.1 (Apache-2.0;
some bundled components carry their own licenses) and the managed component
`espressif/esp_tinyusb` 3.0.1 (Apache-2.0), pinned in
`targets/esp32s3/agent/dependencies.lock`. esp_tinyusb bundles TinyUSB (MIT,
Copyright (c) 2012-2026 hathach). Neither is vendored here: the IDF install
provides one, the component manager fetches the other at build time. Binaries
also contain the IDF second-stage bootloader. When redistributing them, retain
the ESP-IDF, esp_tinyusb and TinyUSB license texts and notices of the
components actually linked. See `targets/esp32s3/PORTING.md`.

## WCH CH32V20x SDK (external)

The CH32V203 port builds selected files from openwch/ch32v20x at
`b375bbbc7dfe8642d6fb7c17e6d3207ee1c3c9c1`.
The SDK is not vendored. Its startup, peripheral drivers and USB examples carry
WCH copyright notices and restrict use (including modified sources and binaries)
to microcontrollers manufactured by Nanjing Qinheng Microelectronics.
`targets/ch32v203/prepare_sdk.py` retains these notices in adapted build-tree
copies. See the original file headers and `targets/ch32v203/PORTING.md`.
