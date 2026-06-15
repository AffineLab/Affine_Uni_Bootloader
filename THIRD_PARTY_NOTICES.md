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

## BSD 3-Clause License Text

Some vendored STMicroelectronics HAL/LL files identify the applicable license through the STM32Cube package license metadata as BSD 3-Clause. The BSD 3-Clause license text is reproduced below for those components.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES, INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT, INCLUDING NEGLIGENCE OR OTHERWISE ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
