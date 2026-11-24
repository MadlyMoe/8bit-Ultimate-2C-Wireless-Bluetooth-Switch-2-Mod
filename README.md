# 8bit Ultimate 2C Wireless Bluetooth Switch 2 Mod

Use an **8BitDo Ultimate 2C Wireless** directly with a Nintendo Switch 2 over Bluetooth. The rear **BT** position runs the Switch 2 controller profile. The **2.4G** position keeps the stock receiver path. Restoring stock firmware returns BT mode to its original Android profile.

**Supported target:** direct-USB vendor model **301B**, original firmware **1.09**, and the bootloader and stock bank hashes checked by the tools. The Wired model, Ultimate 2C Bluetooth edition, Ultimate 2 Wireless and receiver have different firmware. A matching name on the case alone is insufficient.

## Install with your coding agent

On a Windows PC, clone this repository and open the folder in an agent with local terminal and USB access. Connect the controller directly with a USB data cable. Paste this prompt:

> Help me install this mod on my own controller. Read AGENTS.md and docs/INSTALL.md. Run the Windows tools yourself. Identify my controller, save and verify my own full backup, build the image, and show me the target and recovery result before asking me to approve the flash. Ask me only for physical cable, button or console steps. Verify the installed bytes after restart and help me pair and test it. Keep all backups, keys and diagnostics private.

For manual installation, follow the same [installation and recovery guide](docs/INSTALL.md). Standard installation needs a .NET 9 SDK capable of running the supplied Windows tools. The prebuilt patch recipe lets you generate the image locally without a TC32 compiler. The repository distributes source code and a patch recipe. Your stock firmware and calibration come from your own controller.

## Use it on the Switch 2

1. For first registration, open **Controllers â†’ Change Grip/Order**. Unplug the controller from USB, select **BT**, tap **Home**, and hold the top **Pair** button for about three seconds. Press **L + R** when the console shows the controller.
2. For later connections, select **BT** and tap **Home** once. The front light blinks while connecting and the current build requests a solid light once the console has enabled input.
3. To find the connected controller, open **Controllers â†’ Search by Controller Rumble** and hold its icon. The two vibration motors respond to the console's Search command.

## What has been tested

On one Wireless 301B, the owner confirmed native Switch 2 pairing, reconnect with Home, D-pad and button input, and Search rumble. That confirmation was on build `62d50b64`. The current build `571b2b02` changes the connected indicator from player-number pulses to a steady light. [Compatibility and test evidence](docs/COMPATIBILITY.md)

The firmware reuses stock stick calibration and reports both sticks, digital triggers, battery level, primary buttons and rumble. This model has ERM vibration motors. NFC, motion, headset audio, and waking a deeply sleeping controller from Search are outside the implemented feature set.

## Source and credits

- [Build and verify from source](docs/BUILD.md)
- [Firmware architecture and stock bindings](docs/STOCK_ABI.md)
- [Project attributions and third-party notices](THIRD_PARTY.md)

The original firmware modifications, build scripts and documentation use the [MIT license](LICENSE). The Windows utilities link Pad8Lab code under [GPL-3.0](host/vendor/Pad8Lab/LICENSE). tiny-AES-c retains [the Unlicense](firmware/vendor/tiny-aes/unlicense.txt). This independent project is unaffiliated with 8BitDo and Nintendo.
