# Rebuild and verify

Most owners only need [Prepare.ps1](../Prepare.ps1). It reconstructs the release image offline from your exact stock application plus the checked-in patch recipe. Once the owner's verified backup is at `private/controller-original.bin`, prepare and test the package without a connected device:

```powershell
.\Prepare.ps1 -FromBackup
```

This builds the Windows tools, runs the writer's simulated install and recovery checks, generates the image and verifies its hash. The expected application SHA-256 is published in [INSTALL.md](INSTALL.md).

## Compile the embedded source

The firmware uses the Telink TC32 instruction set. The following commands run on Windows with Python 3.10+, the .NET 9 SDK, and a native C99 compiler such as Clang available on PATH. A private stock 301B v1.09 dump from the intended unit belongs in `private/controller-original.bin`.

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r tools/requirements.txt
.\.venv\Scripts\python.exe tools/bootstrap_tc32.py
.\.venv\Scripts\python.exe tools/build.py
.\.venv\Scripts\python.exe tools/tc32_workbench.py
.\.venv\Scripts\python.exe tools/link_firmware.py
.\.venv\Scripts\python.exe tools/package_firmware.py
Get-FileHash build/release/switch22c-301b-v109-full.bin
```

The bootstrapper downloads the compiler at a pinned revision and verifies each selected tool. Native tests exercise protocol, pairing, bond storage, feature negotiation, ATT, input conversion, rumble and the target loop. The linker verifies hook locations and stock bytes. Packaging audits RAM and reviewed stack paths, produces the full application under `build/release`, and regenerates the patch recipe. Compare the final application hash to the published release. Debug ELF and audit file hashes can depend on the build path.

The release uses application flash `0x6000..0x1dfff`. Its code extension starts at `0x19000` and its pairing journal uses `0x3d000..0x3efff`. See the [stock integration notes](STOCK_ABI.md) for the target bindings. Rebuild the .NET writer after changing a recipe because it embeds the recipe in the executable.
