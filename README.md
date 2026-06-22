# UefiToolsPkg

UefiToolsPkg is a small EDK II package for UEFI utilities.

It currently contains:

- `NullAddressProbe`, a UEFI Shell / UEFI Application that intentionally loads from virtual address `0x0` to test whether firmware null-pointer detection is enforced by page tables.
- `LoongArchMappingDump`, a LOONGARCH64 UEFI Shell / UEFI Application that dumps the page-table walk and current TLB entry for a specified virtual address.
- `PciOptionRomInfo`, a UEFI Shell / UEFI Application for inspecting PCI/PCIe Option ROMs exposed by firmware through `EFI_PCI_IO_PROTOCOL`.
- `PciTopology`, a UEFI Shell / UEFI Application that prints a firmware-visible PCI/PCIe topology tree similar to the relationship view from Linux `lspci -tv`.

`NullAddressProbe` prints a warning, then executes an architecture-specific load from virtual address `0x0` using inline assembly. If firmware page tables enforce NULL pointer detection, the application should stop at that load with a CPU exception. If it prints `DONE`, address `0x0` is readable in the current firmware mapping.

This is intentionally a fault-injection diagnostic. Run it only in a VM, lab system, or controlled firmware shell session.

`LoongArchMappingDump` is a non-faulting LOONGARCH64 MMU diagnostic. It does
not load from the target address. Instead, it prints the firmware page-table
walk and searches the current TLB for a live entry that covers the requested
virtual address. If no argument is provided, it dumps virtual address `0x0`:

```text
LoongArchMappingDump.efi
LoongArchMappingDump.efi 0x0
LoongArchMappingDump.efi 0x90000000
```

The output includes:

- `PGDL`, `PGDH`, `PWCTL0`, `PWCTL1`, `STLBPGSIZE`, and direct-map-window CSRs
- the selected page-table root and each walked level
- decoded entry bits: `V`, `D`, `PLV`, cache attribute, `G/Huge`, `P`, `W`, `Bit12`, `NR`, `NX`, and `RPLV`
- entry kind hints for table pointers, huge leaves, page leaves, and invalid entries
- `TLBSRCH` / `TLBRD` output when a current TLB entry covers the target address

For non-leaf table pointers, `Bit12` is part of the next-table physical page
number, not `HGlobal`. For huge-leaf entries, bit 6 is `HUGE` / `G` and bit 12
is `HGLOBAL`. The tool prints the interpreted entry kind to avoid confusing
table-pointer address bits with huge-page attributes.

`PciOptionRomInfo` enumerates PCI I/O handles, skips devices without an Option ROM, then prints:

- PCI segment / bus / device / function
- Vendor ID, Device ID, class code, header type
- `RomImage` pointer and `RomSize`
- Option ROM image offset and image length
- PCIR metadata: signature, Vendor ID, Device ID, revision, code revision, class code, structure length
- Option ROM `CodeType` (legacy PC-AT or EFI)
- EFI image machine type / architecture when present, including LOONGARCH64
- EFI subsystem, compression type, initialization size, and image header offset

The parser includes bounds checks for truncated headers, invalid PCIR offsets, invalid image lengths, and multi-image Option ROMs.

`PciTopology` enumerates PCI I/O handles, reads config space, identifies PCI-to-PCI bridges, and prints the firmware-visible topology inferred from each bridge's primary / secondary / subordinate bus numbers. Example output shape:

```text
PCI Topology (EFI_PCI_IO_PROTOCOL scan)
Devices=5 Bridges=1

Domain 0000
+- 00:00.0 [060000] Host bridge 0014:7a00
`- 00:01.0 [060400] PCI bridge 0014:7a01 -> bus 01-01 primary=00
   +- 01:00.0 [010802] Mass storage 1d0f:8061
   `- 01:01.0 [020000] Network 1af4:1000
```

Note: this is the firmware-enumerated view exposed through `EFI_PCI_IO_PROTOCOL`; devices not enumerated by firmware will not appear.

## Package layout

- `UefiToolsPkg.dec`
- `Applications/NullAddressProbe/NullAddressProbe.c`
- `Applications/NullAddressProbe/NullAddressProbe.inf`
- `Applications/LoongArchMappingDump/LoongArchMappingDump.c`
- `Applications/LoongArchMappingDump/LoongArchMappingDump.inf`
- `Applications/PciOptionRomInfo/PciOptionRomInfo.c`
- `Applications/PciOptionRomInfo/PciOptionRomInfo.inf`
- `Applications/PciTopology/PciTopology.c`
- `Applications/PciTopology/PciTopology.inf`
- `Applications/FillNvVars/FillNvVars.c`
- `Applications/FillNvVars/FillNvVars.inf`
- `UefiToolsPkg.dsc`

## Build with EDK II

This repository is an EDK II package, not a complete standalone EDK II workspace. Build it from an existing EDK II workspace and expose the package directory through `PACKAGES_PATH`.

Example using generic workspace variables. Set `WORKSPACE` to the directory that contains `UefiToolsPkg`, set `EDK2_DIR` to your EDK II checkout, and export `PACKAGES_PATH` so EDK II can find both trees:

```sh
: "${WORKSPACE:?set WORKSPACE to the directory containing UefiToolsPkg}"
: "${EDK2_DIR:?set EDK2_DIR to your EDK II checkout}"
export PACKAGES_PATH="$WORKSPACE:$EDK2_DIR"
cd "$EDK2_DIR"
source edksetup.sh
build -p UefiToolsPkg/UefiToolsPkg.dsc \
  -m UefiToolsPkg/Applications/PciTopology/PciTopology.inf \
  -a LOONGARCH64 -t GCC -b DEBUG
```

For the null-address probe:

```sh
build -p UefiToolsPkg/UefiToolsPkg.dsc \
  -m UefiToolsPkg/Applications/NullAddressProbe/NullAddressProbe.inf \
  -a LOONGARCH64 -t GCC -b DEBUG
```

For the LoongArch mapping dump tool:

```sh
build -p UefiToolsPkg/UefiToolsPkg.dsc \
  -m UefiToolsPkg/Applications/LoongArchMappingDump/LoongArchMappingDump.inf \
  -a LOONGARCH64 -t GCC -b DEBUG
```

For the existing Option ROM scanner:

```sh
build -p UefiToolsPkg/UefiToolsPkg.dsc \
  -m UefiToolsPkg/Applications/PciOptionRomInfo/PciOptionRomInfo.inf \
  -a LOONGARCH64 -t GCC -b DEBUG
```

For the NV variable store fill helper:

```sh
build -p UefiToolsPkg/UefiToolsPkg.dsc \
  -m UefiToolsPkg/Applications/FillNvVars/FillNvVars.inf \
  -a X64 -t GCC -b DEBUG
```

`FillNvVars` is intended for QEMU/OVMF diagnostic reproduction. It writes
dummy non-volatile variables until the firmware variable service reports an
error, then resets the system so the same variable-store image can be reused
for the next boot phase.

For a platform DSC, add the module INF to the DSC `[Components]` section and build it with the platform:

```ini
[Components]
  UefiToolsPkg/Applications/PciOptionRomInfo/PciOptionRomInfo.inf
```

Then build with your normal platform command, for example:

```sh
: "${WORKSPACE:?set WORKSPACE to the directory containing UefiToolsPkg}"
: "${EDK2_DIR:?set EDK2_DIR to your EDK II checkout}"
export PACKAGES_PATH="$WORKSPACE:$EDK2_DIR"
cd "$EDK2_DIR"
source edksetup.sh
build -p YourPlatformPkg/YourPlatform.dsc \
  -m UefiToolsPkg/Applications/PciOptionRomInfo/PciOptionRomInfo.inf \
  -a LOONGARCH64 -t GCC -b DEBUG
```

Adjust `WORKSPACE`, `PACKAGES_PATH`, architecture, toolchain tag, and platform DSC path for your local EDK II tree.

## License and attribution

UefiToolsPkg is licensed under the Apache License, Version 2.0.

Redistributions of source or binary forms must retain the Apache-2.0 license
text, copyright notices, and applicable NOTICE attribution. Modified files must
carry prominent notices stating that changes were made, as required by
Apache-2.0.

Suggested attribution:

```text
UefiToolsPkg by MarsDoge
https://github.com/MarsDoge/UefiToolsPkg
```
