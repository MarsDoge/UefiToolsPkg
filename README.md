# PciOptionRomInfo

PciOptionRomInfo is a small UEFI Shell / UEFI Application for inspecting PCI/PCIe Option ROMs exposed by firmware through `EFI_PCI_IO_PROTOCOL`.

It enumerates PCI I/O handles, skips devices without an Option ROM, then prints:

- PCI segment / bus / device / function
- Vendor ID, Device ID, class code, header type
- `RomImage` pointer and `RomSize`
- Option ROM image offset and image length
- PCIR metadata: signature, Vendor ID, Device ID, revision, code revision, class code, structure length
- Option ROM `CodeType` (legacy PC-AT or EFI)
- EFI image machine type / architecture when present, including LOONGARCH64
- EFI subsystem, compression type, initialization size, and image header offset

The parser includes bounds checks for truncated headers, invalid PCIR offsets, invalid image lengths, and multi-image Option ROMs.

## Source files

- `PciOptionRomInfo.c`
- `PciOptionRomInfo.inf`

## Build with EDK II

This repository is an EDK II module, not a complete standalone EDK II workspace. Build it from an existing EDK II workspace and expose this repository as a package path.

Example using `/home/qdy/ModernSetupPkg/External/edk2` as `WORKSPACE` and `/home/qdy` as the package path that contains this repo:

```sh
export WORKSPACE=/home/qdy/ModernSetupPkg/External/edk2
export PACKAGES_PATH=/home/qdy:$WORKSPACE
cd "$WORKSPACE"
source edksetup.sh
build -p ShellPkg/ShellPkg.dsc -m pci-option-rom-info/PciOptionRomInfo.inf -a X64 -t GCC5 -b DEBUG
```

For a platform DSC such as ModernSetupPkg, add the module INF to the DSC `[Components]` section and build it with the platform:

```ini
[Components]
  pci-option-rom-info/PciOptionRomInfo.inf
```

Then build with your normal platform command, for example:

```sh
export WORKSPACE=/home/qdy/ModernSetupPkg/External/edk2
export PACKAGES_PATH=/home/qdy:$WORKSPACE
cd "$WORKSPACE"
source edksetup.sh
build -p ModernSetupPkg/Experimental/ModernSetupApp.dsc \
  -m pci-option-rom-info/PciOptionRomInfo.inf \
  -a LOONGARCH64 -t GCC -b DEBUG
```

Adjust `WORKSPACE`, `PACKAGES_PATH`, architecture, toolchain tag, and platform DSC path for your local EDK II tree.

## License

BSD-2-Clause-Patent, matching common EDK II project style.
