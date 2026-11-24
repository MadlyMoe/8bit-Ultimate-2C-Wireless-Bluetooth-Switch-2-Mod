# Installing on an owner's controller

Start in the repository root on the owner's Windows PC. Read [the installation guide](docs/INSTALL.md) for exact commands. Use the released recipe for installation. Rebuilding the embedded code is a separate developer workflow.

1. Identify the direct-cable application interface. Confirm vendor model 301B and firmware 1.09. Use a separate private working folder or fresh checkout for each controller.
2. Prepare the image from this owner's original 512 KiB backup. Save the original permanently. For first install, compare the entire device with the backup in resident recovery. Keep the resulting hash for restoration. If the controller already contains this mod, locate its original backup before proceeding.
3. Show the owner the exact target, full-image SHA-256 and recovery result. Obtain their explicit approval before installing or restoring. Keep the controller connected throughout programming and verification.
4. Perform the install. After a successful full-device readback, wait for normal USB startup and run the independent application comparison. Preserve the saved pairing journal on upgrade.
5. Guide the owner through first registration or saved-pair reconnect as applicable. Ask for the smallest set of physical observations that distinguishes working input, rumble and connection stability. Describe physical results separately from simulations.
6. On an interruption, inspect the installer log to establish whether an erase began. Follow the recovery guide. Keep backups, device addresses, pairing keys, binary dumps and unredacted diagnostics on the owner's machine.

A different bootloader, original application, occupied extension, factory data mismatch or pending vendor update calls for stopping and identifying the mismatch. Keep the hash, device and range checks in effect. Never use another owner's backup. The owner can run every command manually if they prefer.
