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

Example using generic workspace variables. Set `WORKSPACE` to the directory that contains this repository, set `EDK2_DIR` to your EDK II checkout, and export `PACKAGES_PATH` so EDK II can find both trees:

```sh
: "${WORKSPACE:?set WORKSPACE to the directory containing uefi-tools}"
: "${EDK2_DIR:?set EDK2_DIR to your EDK II checkout}"
export PACKAGES_PATH="$WORKSPACE:$EDK2_DIR"
cd "$EDK2_DIR"
source edksetup.sh
build -p ShellPkg/ShellPkg.dsc -m uefi-tools/PciOptionRomInfo.inf -a X64 -t GCC5 -b DEBUG
```

For a platform DSC, add the module INF to the DSC `[Components]` section and build it with the platform:

```ini
[Components]
  uefi-tools/PciOptionRomInfo.inf
```

Then build with your normal platform command, for example:

```sh
: "${WORKSPACE:?set WORKSPACE to the directory containing uefi-tools}"
: "${EDK2_DIR:?set EDK2_DIR to your EDK II checkout}"
export PACKAGES_PATH="$WORKSPACE:$EDK2_DIR"
cd "$EDK2_DIR"
source edksetup.sh
build -p YourPlatformPkg/YourPlatform.dsc \
  -m uefi-tools/PciOptionRomInfo.inf \
  -a LOONGARCH64 -t GCC -b DEBUG
```

Adjust `WORKSPACE`, `PACKAGES_PATH`, architecture, toolchain tag, and platform DSC path for your local EDK II tree.

## License

BSD-2-Clause-Patent, matching common EDK II project style.
