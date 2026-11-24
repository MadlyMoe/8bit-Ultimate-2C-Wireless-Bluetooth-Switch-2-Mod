# Wireless 301B / v1.09 integration evidence

All addresses below refer to the exact application SHA-256 recorded in
`tools/image.py`. The bindings below come from static analysis and simulated ABI checks.
The reference owner confirmed input and Search vibration on a physical Switch 2. The stock bootloader, calibration and second bank remain intact.

## Attribute callbacks

The stock `l2cap_att_handler` entry is `0x138c0`, called from the CID=4 branch
at `0x14aaa` of `blc_l2cap_packet_receive` (`0x14a58`). It preserves its packet
argument from r1 in r5 (`0x138ce`). In the write-command path:

- `0x13f92..0x13fa4` loads bytes 16..19 of the selected 24-byte attribute into r3.
- `0x13faa` moves r5 (packet) to r0.
- `0x13fac` calls `0x14524`, whose instruction is `tjex r3`.
- `0x13fb0..0x13fc4` handles the callback return and Write Without Response
  opcode 0x52.

Thus the supported callback ABI is `int callback(void *packet)`. The suggested
two-argument callback in upstream `analysis-btff.md` is **not** this ABI. Packet
fields are LL header/length at +0/+1, L2CAP length LE16 at +2, CID LE16 at +4,
ATT opcode at +6, handle LE16 at +7, and write value at +9. Bounds must use the
reassembled L2CAP length and the configured receive capacity before parsing.

`bls_att_setAttributeTable` at `0x13628` stores the table pointer in `0x843648`.
Its shorter inner entry at `0x1362c` has the same bytes as several callback
registration functions. A signature match alone cannot identify its semantics.

## Notification and packet storage

The notification wrapper at `0x1458c` is a semantic match for
`blc_gatt_pushHandleValueNotify(connection, handle, data, length)`. It checks
MTU via `0x842f20`, builds ATT opcode 0x1b and calls `0x1484c` for fragmentation.
The latter requires FIFO headroom for all fragments plus stack-reserved slots.
This is not yet a hardware-validated binding.

Corrected FIFO names, confirmed against the SDK init routines and stock users:

| Stock allocation | Initializer | Role |
| --- | --- | --- |
| `0x843178..0x8432b8`, 8 x 40 bytes | `0x11e64`, call `0xa152` | TX FIFO |
| `0x8432b8..0x8433b8`, 4 x 64 bytes | `0x11ea4`, call `0xa15c` | RX FIFO |
| `0x8436c0`, 32 bytes | `0x1478c`, call `0xa17c` | L2CAP reassembly |

Earlier working notes had RX/TX labels swapped. Neither area is free SRAM.
The TX initializer requires size multiple of 4; RX requires multiple of 16
and a power-of-two slot count >=4. Shrinking buffers by intuition is invalid.
TX FIFO state is `0x843a20`, RX state `0x843a6c`; `getTxFifoNumber` has low-RAM
execution alias `0x1abc` (physical code at flash `0x7abc`). Do not call a
RAM-resident function through its flash location or its SRAM data alias.

## SRAM exclusions

`0x840000..0x842500` contains RAM code. `0x842500..0x842600` is cache tag storage.
The following `0x842600..0x842e00` is excluded as likely cache data: the Telink
linker reserves +0x900 after the cache boundary, comprising 256 bytes tags plus
2048 bytes data. No assumption that this gap is general-purpose SRAM is valid.
IRQ stack is `0x842fb0..0x843130`. Main stack is `0x843cf0..0x844000` (784 bytes).
The public SDK reserves at least 600 bytes, and actual call-chain usage needs
checking before any reservation in this interval.

The SDK's ATT response array is 256 bytes, but this stock binary's array is only
32 bytes (`843bd4..843bf4`). Its next object is the L2CAP control at `843bf4`.
SMP allocations occupy `843c04..843cd8`; `843cd8..843cf0` is OTA state and MUST
NOT be treated as SMP scratch. Proposed overlays remain conditional on removal
of every stock writer and a bounded main/IRQ stack analysis.

## Remaining full-image requirements

The proprietary profile source contains 59 attributes, with commands at 0x14
and 0x16, replies at 0x1a and 0x1e, and inputs at 0x0e. Its callback and state
symbols are now fully linked to the exact-image adapters. The generated profile
uses our bounded ATT server, not the stock table ABI. The final ELF has no
unresolved symbols or unallocated writable data. See integration-audit.json for
reviewed stack chains and reclaimed-region literal references. These static
checks do not replace runtime high-water measurements or console verification.

## Radio and stock bindings traced on 2026-11-23

- BLE initializer `a10c` called at `a4bc`; BLE loop `a298` called at `a4a4`.
  Keeping the mode selection outside these replacements preserves 2.4G paths.
- L2CAP callback registration `14780` -> `843650`. Call at `1268c` passes
  connection `0x40` and plaintext LL header pointer in r1 (DMA pointer + 4).
- LTK callback registration `121bc` -> `8435f8`; arguments connection, RAND
  pointer, EDIV. Reply `130d8`, negative reply `13114`. HCI uses raw A1 XOR B1.
  Encryption enabled is `8435cc + 38`, not the busy/error flag at +39.
- Direct L2CAP enqueue `1484c` takes connection, CID, prefix, prefix length,
  payload, payload length. It uses 304 local stack bytes plus saved registers.
  It still reads negotiated MTU at `842f22`, even when stock ATT is bypassed.
- CONNECT_IND interval minimum is the subtraction at `11938` (6); upper
  range constant `11a00` is 3194. Both must change for minimum 4, maximum 3200.
  Accepted setup call `119f4 -> 12cf8` receives the DMA packet in r0.
- LL control interception at `1274e` handles PHY_REQ/RSP/UPDATE_IND. The instant
  path at `121e6` schedules the modular event counter transition. Feature mask
  `842e6c` enables 2M/CSA2 only with both target implementations installed.
- Radio channel load at physical `7ce8` executes at alias `1ce8`. blttcon
  `843ac8` holds the map at +7, event counter at +12 and instant at +14.
  Stock `12028..12050` reverses the four CONNECT_IND access address bytes
  into +20 for the radio. At `12052..12058` it copies the original bytes to
  +16. CSA #2 derives its channel identifier from the wire-order copy at +16.
- B80 PHY data `842ed4`, connection PHY `843b0c`, switch callback `8435b0`,
  instant callback `8435b4`; use B80 SDK register sequence, never TLSR8258's.
- Calibrated button word `84342c`; axes `8433ec`, `8433f6`, `8433e4`,
  `8433e6`; digital triggers already use stock threshold >32 at c3c4/c3d2.
- Motor function `f93c` accepts amplitude 0..255 and channel. Timed latch
  `8434f1` must clear before stop; target amplitudes `8434ef/8434f0`.
- Battery millivolts `843424`, stock percentage function `b5a8`.
- LED state `d3d8(id,state)` uses id 4 at `843457` for the front status LED.
  State 1 is steady. The stock pairing path at `d75c` selects state 3.
  The shared LED service at `d4d0` runs every 200 ms. Its renderer `d36c`
  toggles state 3 every tick and state 2 every third tick. The setter at
  `d418` preserves the blink phase when the requested state already matches.
  The outer loop runs the stock long-press check at `a47c`; the BT branch at
  `d156` calls the patched Pair handler at `b8bc`.
- Flash read callback `842e50`, program callback `842e3c` use (address,length,
  buffer). Sector erase is `fe98`. All sectors `19000..3f000` were erased in
  the exact original backup; that observation alone does not authorize use.

## Flash protection and outer BLE loop review - 2026-11-23

The initial runtime omitted the protection step required by stock writes.
Stock main calls `bb0c(1)` at `a38a` to enable protection before selecting the
controller mode. The config writers call `bd00 -> bb0c(0)` before erase/program.
The raw page writer does not perform this step. Empty journals therefore do
not prove the console failed before sending pairing commands.

Exact stock flash-ID/protection-mask cases in `bb0c`:

| JEDEC ID | Mask | Status access |
| --- | --- | --- |
| 1160c8 | 001c | SR1 |
| 1360c8 | 001c | SR1 |
| 114485 | 007c | SR1 |
| 136085 | 407c | SR1+SR2; alternate SR3.WPS mode needs separate handling |

`ff14` reads JEDEC ID. `1025c` reads SR1; `102dc` reads SR1+SR2. `10268` and
`102f8` replace only the supplied mask, preserving all other bits. They use
`feb0`/`fed8` for status commands. The replacement saves the original protection
bits, clears only the known mask, verifies it, performs a bounded journal write,
then restores and verifies the saved bits. It refuses unknown IDs and refuses
136085 with SR3.WPS set instead of altering that extra configuration register.
The stock software protection flag is left unchanged. The host does not send
the vendor USB unprotect command.

`11188` may replace the program callback `fe5d` with `10f5d` depending on stock
feature flags at `843500`. Both are now explicitly accepted; the latter's
52-byte frame is included in the stack audit, rather than assuming the raw
writer's 24-byte frame for every configuration.

After the replaced BLE loop at `a4a4`, stock executes `fa68(0,0,0)` at `a4ae`.
That clears shared motor targets on every iteration. The replacement already
owns scheduling, watchdog and disconnect-stop, so bytes `a4a8..a4b1` contained five intended NOPs
in the c2299c5f through 668692e8 images. The following branch and the wired/2.4G paths are preserved.
Tests of the target loop alone previously missed this outer caller behavior.

The assembler encodes `tnop` as 0x06c0 and `tstorerb r0,[r0,#27]` as
0x46c0. The vendor disassembler labels the latter a NOP as well. The five
hand-coded 0x46c0 halfwords ran immediately after each Bluetooth poll.
Their on-chip behavior at this site has not been measured. The revised
patch emits one ordinary short branch at 0xa4a8, with the same destination
0xa496 as the stock branch at 0xa4b2. The linker checks its encoding and
the complete original motor-clear block before packaging.

The shared foreground call at `a438` runs the physical matrix through the
timer gate at `d234`. After 80,000 stock ticks it calls `c7a4`. At `c8f2`
the scanner writes the raw button word to `84342c`, then `c910` passes
that same address and both trigger values to `c3a8`. A trigger value above
32 sets button bit 14 for the left trigger or bit 15 for the right trigger.
The same routine applies stock remaps. By contrast, `b388` samples the
analog channels and `b14c` applies calibration. The firmware reads the
post-remap word and calibrated axes. Release 62d50b64 retains the original
matrix, remap and analog acquisition instruction bytes.
