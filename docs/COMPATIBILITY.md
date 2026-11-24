# Compatibility and test evidence

The hardware reference is one 8BitDo Ultimate 2C Wireless, direct-cable vendor model 301B, running stock version 1.09 before modification. Other controllers must pass the same stock application and bootloader checks against their own original backup.

| Area | Evidence |
| --- | --- |
| Pairing and reconnect | The Switch 2 displayed a pairing notification. A private two-pass read confirmed a committed bond and an encrypted reconnect. The owner subsequently used Home to reconnect on build 62d50b64. |
| Controls and vibration | On 62d50b64 the owner confirmed working D-pad and buttons and vibration from Search by Controller Rumble. A separate per-button sweep, full stick-range measurement, game-specific haptics and long battery-life run have no physical test record. |
| Connection continuity | Two earlier sessions ended when the controller rejected a profile query after input subscription. 62d50b64 answers that query and keeps an active link through unsupported optional writes. In the target simulation, 27,000 consecutive reports over 135 seconds carried alternating L+R and D-pad input and both stick extremes. |
| Connected light | 571b2b02 selects the stock steady-on indicator after encryption and input subscription. The simulated target holds it across 27,000 reports and player-light requests. The reference unit received this revision and the complete 512 KiB readback matched. A second normal-USB check confirmed its 98,304-byte application and preserved pairing journal. Visual confirmation of the updated LED is pending. |
| Radio | Hardware logs showed an accepted saved key, negotiated MTU 512, channel selection algorithm 2, and 2M PHY. Windows also discovered the services and read the device name and appearance. |
| Wired and 2.4 GHz | Source and byte comparisons retained the wired and receiver dispatch, receiver initialization, button matrix and stick calibration paths. Normal USB returned after update. Physical receiver gameplay has no separate acceptance record. |
| Recovery | The original 512 KiB backup was verified over both normal USB and resident recovery on the reference unit. The writer verifies protected regions and full readback. Simulations cover interruptions and exact restoration. A live stock restoration on the reference unit has not been performed. |
| Physical limits | Two ERM motors, one status indicator. No IMU, NFC, audio or mouse capability is advertised. Stock L4/R4 remaps can feed ordinary buttons. Independent GL/GR, charge-state flags and receiving a console wake advertisement in deep sleep are unverified or unimplemented. |

The firmware build and the Windows utilities have separate roles. The firmware is pinned to the reviewed stock image. The public installer accepts different factory data only when the connected device matches the owner's own protected bytes. Successful checks on another device establish compatibility for that particular unit.
