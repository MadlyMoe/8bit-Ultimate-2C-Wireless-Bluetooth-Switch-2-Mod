# Install on Windows and recover stock firmware

Use one checkout per physical controller. Keep the original backup outside the checkout as well. Use the direct USB cable for all reads and writes. The receiver is a different device.

## 1. Prepare in normal USB mode

Install a Windows .NET 9 SDK and connect the controller normally. Start PowerShell in the repository root. The preparation script builds the two utilities, identifies the controller, reads all 512 KiB twice for the original backup, runs simulated writer tests, creates the image and checks the installation plan.

```powershell
.\Prepare.ps1
```

The expected normal interface is `2dc8:310a`, internal model `301b`, vendor version `1.09`. The expected target is **98,304 bytes**, SHA-256 **571b2b024a0f20202e50a07735cd9fdcf91e75e5f5560c59ed5d024a39f35c10**. Keep `private/controller-original.bin` and its printed SHA-256. A backup contains per-unit data. For an already-modified controller, supply the original saved before its first install.

## 2. Verify recovery before first installation

Unplug USB, hold **LB + RB** (the two upper shoulder buttons), and reconnect the cable while holding them. Release them after connection. The light can be dark in recovery. Run:

```powershell
$reader = 'host/bin/Release/net9.0/ControllerRead.dll'
$writer = 'host-flash/bin/Release/net9.0/ControllerFlash.dll'
$original = 'private/controller-original.bin'
$image = 'build/switch22c-301b-v109.bin'
dotnet $reader recovery-check $original
```

The recovery interface is `2dc8:3208`, internal model `301b`. Continue after the first-time comparison matches every byte. For an upgrade from a recognized custom release, the device already differs from stock. Use the same unit's original backup and let the installer check the current image, protected regions and pairing journal.

## 3. Install and verify

The installation step erases and replaces application sectors. Obtain the owner's approval and keep the USB cable connected through the final readback and restart. Use the variables above in the same shell.

```powershell
dotnet $writer install $original $image --approved-sha256 571b2b024a0f20202e50a07735cd9fdcf91e75e5f5560c59ed5d024a39f35c10
```

The installer saves a separate preflash snapshot before the first erase and compares all 512 KiB afterward. It preserves factory calibration, bootloader, second bank and the existing pairing journal. After the controller restarts in normal USB mode, compare the running application's flash again:

```powershell
dotnet $reader verify-image $image
```

The expected result includes the 98,304-byte count and the release hash above. Then unplug USB, select BT and follow the [console instructions](../README.md#use-it-on-the-switch-2).

## Recovery after interruption

Open the latest local log under `private/flash-records`. If the operation stopped during the first read and the log contains no erase, the installed firmware is intact. Reconnect using a known data cable and direct port, and retry. If erasing had already begun, keep the unit in recovery. If necessary, re-enter recovery with LB + RB. The restore operation accepts an interrupted application and checks the bootloader and protected data against the owner's original.

Restoring stock also replaces custom pairing records. Obtain explicit approval for restoration. Compare the backup hash with the one recorded at first installation before passing it to the tool.

```powershell
$backupHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $original).Hash
dotnet $writer restore $original --approved-backup-sha256 $backupHash
```

Wait for verified full readback and normal USB restart. Keep the log. The read-only `identify` command then checks the model and vendor version. A second recovery-mode `recovery-check` after stock restore can independently compare every byte.

## Diagnosing a wireless attempt

The front indicator requests a fast blink during open pairing, a slow blink during reconnect, and a steady light once an encrypted console session is receiving input reports. A returning owner can use Home with their existing pairing. Holding Pair opens registration.

After a failed wireless attempt, reconnect normal USB. Save private diagnostics in a new folder:

```powershell
dotnet $reader diagnose private/session-01
```

The reader compares two passes and reports the latest recorded events, subscription state, button-report and motor-drive flags. A sudden power cut may leave only startup events. A queued report records the controller's action. Use observations from the console to confirm actual input and vibration.
