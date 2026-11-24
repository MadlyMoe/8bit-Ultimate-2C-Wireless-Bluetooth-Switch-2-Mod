# Third-party notices

`firmware/src/s2_protocol.c` adapts protocol facts, pairing, advertising and
late-initialization reply constants from esp-cpp/espp, commit
`61630390dec81d8130956a8df899382a7c2c613d`:
https://github.com/esp-cpp/espp/tree/61630390dec81d8130956a8df899382a7c2c613d/components/switch2_pro

MIT License

Copyright (c) 2022 esp-cpp

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

The independently documented packet header and feature-query layout is from
https://github.com/ndeadly/switch2_controller_research/blob/master/commands.md .

The AES implementation is retained verbatim with its notice in
`firmware/vendor/tiny-aes/unlicense.txt`. Windows HID transport and read protocols
retain Pad8Lab's GPL-3.0 notice and license under `host/vendor/Pad8Lab/`.
The local reverse-engineering dependency is pyelftools 0.32 (public-domain
license), installed only into `.venv`.

Private controller backups, vendor firmware and SDK archives are local analysis
inputs. They are excluded from distribution; this project does not grant rights
to redistribute them. No Nintendo proprietary firmware is distributed here.

The generated GATT profile and channel-selection reference also use the MIT
project https://github.com/ClosetPie107/8bitdo-u2w-custom-firmware at commit
1b0756deb3082217b169e73a5aa0ad65daedde10. Its firmware targets DIFFERENT hardware
and is never flashed here. Its MIT notice follows.

MIT License

Copyright (c) 2026 8BitDo Ultimate 2 Wireless Custom Firmware Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.


B80 radio-register sequences were checked against the local Telink B80 BLE SDK
reference (tc_ble_simple_sdk/b80_ble_sdk), not the TLSR8258 sequences. The SDK is a private
analysis input; no SDK binary is redistributed by the patch package. Exact
Wireless v1.09 stock input/boot findings were cross-checked against
https://github.com/VtnDenis/8bitdo-ultimate-2c-switch2 at
 aecb9c8561fcd7346234ba61cfc3c9b1324cf47f and against this unit's own backup.
Neither reference supplies a validated native Switch 2 BLE firmware for this unit.


## Sources used during development

| Project or source | Contribution |
| --- | --- |
| [JehuAlv/Pad8Lab](https://github.com/JehuAlv/Pad8Lab/tree/14b55c42565c85a90287799e399adaa7cc3204a9) | Windows HID transport, bootloader protocol, normal USB reads and the model-specific update procedure. The vendored source, its local modifications and both combined Windows tools retain GPL licensing. |
| [jgus-org/8bitdo-2c-re-flake](https://github.com/jgus-org/8bitdo-2c-re-flake) | Stock Wireless image layout, firmware formats and TC32 analysis workbench. |
| [VtnDenis/8bitdo-ultimate-2c-switch2](https://github.com/VtnDenis/8bitdo-ultimate-2c-switch2/tree/aecb9c8561fcd7346234ba61cfc3c9b1324cf47f) | Same-image stock input packing, calibration and USB protocol research. This build uses the Wireless model's native Bluetooth path. |
| [Leon's Notes](https://leonsnotes.ca/2026/04/04/reverse-engineering-the-switch-2-pro-controllers-bluetooth-protocol/) | Independent account of console connection and initialization behavior. |
| [ndeadly/switch2_controller_research](https://github.com/ndeadly/switch2_controller_research) | Captured Bluetooth initialization, command replies, calibration layout, inputs and Search vibration. |
| [esp-cpp/espp](https://github.com/esp-cpp/espp/tree/61630390dec81d8130956a8df899382a7c2c613d/components/switch2_pro) | MIT-licensed Switch 2 profile reference. See the retained notice above. |
| [zhantss/ESP32-BLE5-NSController-Emulator](https://github.com/zhantss/ESP32-BLE5-NSController-Emulator) | Compared pairing, startup and profile-query handling on another BLE stack. |
| [ClosetPie107/8bitdo-u2w-custom-firmware](https://github.com/ClosetPie107/8bitdo-u2w-custom-firmware/tree/1b0756deb3082217b169e73a5aa0ad65daedde10) | GATT profile and channel selection reference. The MIT notice is retained above. |
| [ascendedent/8bitdo-Linux-Software](https://github.com/ascendedent/8bitdo-Linux-Software) | Wireless controller firmware identification and update-server research. |
| [telink-semi/tc_ble_simple_sdk](https://github.com/telink-semi/tc_ble_simple_sdk) | B80 chip-family and radio register semantics. The SDK is an external reference. |
| [trust1995/Ghidra_TELink_TC32](https://github.com/trust1995/Ghidra_TELink_TC32/tree/f466dac5f746ac65d57dbbf8eb534988e1076f53) | TC32 instruction descriptions and pinned Windows cross-toolchain. The bootstrapper obtains the upstream tools locally. |
| [libsdl-org/SDL](https://github.com/libsdl-org/SDL/blob/main/src/joystick/hidapi/SDL_hidapi_switch2.c) and [seedds/nxnative](https://github.com/seedds/nxnative/blob/HEAD/dm/host/switch2_proto.h) | Rumble encoding and report behavior cross-checks for the independently written ERM decoder. |
| [kokke/tiny-AES-c](https://github.com/kokke/tiny-AES-c) | AES-128 implementation used for pairing. The upstream Unlicense accompanies the code. |
| [eliben/pyelftools](https://github.com/eliben/pyelftools) | ELF loading and DWARF stack analysis in the developer build. |
| [Bluetooth SIG](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-61/out/en/host/attribute-protocol--att-.html) and [Nintendo Support](https://www.nintendo.com/au/support/articles/how-to-use-the-search-for-controllers-feature-on-nintendo-switch-2/) | ATT write-command behavior and the console's Search procedure. |

The MIT license at the repository root covers original project code and documentation subject to file-specific notices. For distribution of the Windows programs, use the GPL terms and the corresponding source under `host/` and `host-flash/`. The public patch contains the original extension and a small set of stock instruction preimages. The owner supplies 8BitDo's stock application from their own device.
