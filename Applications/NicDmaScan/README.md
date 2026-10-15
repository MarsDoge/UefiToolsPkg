# NicDmaScan: read-only NIC DMA inspection

NicDmaScan inventories firmware-visible PCI network devices and inspects ring
registers for the allowlisted Realtek `10ec:8168`, PCI revision `0x15` layout.
PCI revision does not identify the exact MAC version. Device locations and BAR
addresses are discovered at runtime, not copied from an OS inventory.

## Default mode

Run the application matching the firmware architecture from a UEFI Shell:

```text
UefiTools.efi nicdma
UefiTools.efi nicdma --help
NicDmaScan.efi
NicDmaScan.efi --help
```

The default mode enumerates class `02` devices exposed through
`EFI_PCI_IO_PROTOCOL`. It prints Segment:BDF, VID:DID, revision, command/BME,
and the SNP state when SNP is installed on the same handle. SNP child handles
are not correlated; `N/A` does not mean that no network driver exists.

Only matching RTL8168 devices with a type-0 header and the expected BAR layout
are decoded. Other devices report `N/A` without MMIO access. The decoder
requires BAR0 to be I/O, BAR1 unused, and BAR2 64-bit memory. BAR2 must have a
valid, nonzero memory resource covering offset `0xeb`. Reads use
`PciIo->Mem.Read()` on BAR2; BAR4 and Option ROMs are not inspected.

MMIO is skipped when memory decoding is disabled, the device is not in D0,
or the PCI power capability chain is malformed or unreadable. The application
does not enable decoding or change the power state.

| Ring | BAR2 offsets (low/high) | Default output |
| --- | --- | --- |
| Normal TX | `0x20/0x24` | Device-DMA base |
| High-priority TX | `0x28/0x2c` | Raw base; activation is unknown |
| RX | `0xe4/0xe8` | Device-DMA base |

Each register pair is read twice to detect changes. Ring lengths and CPU
physical addresses remain unknown in default mode. A nonzero base or BME=1
is not proof of active DMA.

## Optional descriptor snapshots

```text
UefiTools.efi nicdma --identity-map
NicDmaScan.efi --identity-map
```

Use this option only after independently establishing that device-DMA
addresses, CPU physical addresses, and accessible CPU pointer values are
identical, with coherent memory access. The option asserts this assumption;
it does not detect, verify, or create a mapping.

IOMMUs, bounce buffers, host-bridge translation, non-identity CPU mappings,
unreadable pages, or noncoherent caches can invalidate the assumption. An
address appearing in a RAM memory-map entry is not sufficient. An incorrect
assumption may cause a CPU exception or return unrelated data.

Snapshot limits and interpretation:

- Only normal TX and RX descriptors are read. High-priority TX activation
  and descriptor semantics are not established, so only its base is shown.
- Ring bases must be nonzero and 256-byte aligned.
- Each ring walk is limited to 256 descriptors of 16 bytes each. EOR ends
  the walk. Missing EOR or an early refusal produces an incomplete result,
  not a claimed full ring length.
- Before reading, the entire observed prefix must fit in a single eligible
  memory-map entry. Loader, boot-services, and conventional RAM are accepted
  only with WB attributes and without runtime or read-protection attributes.
  Reserved, ACPI, and MMIO regions are rejected. Crossing even two eligible
  entries is conservatively rejected.
- Address addition, page-count overflow, and pointer-width bounds are checked.
  Descriptors are read twice using byte loads and memory fences. A mismatch
  stops the walk. A changed or unreadable ring base after the walk invalidates
  the preceding ranges.
- Reads do not pause DMA or raise TPL. Matching reads do not establish an
  atomic snapshot or cache coherence; concurrent changes and ABA transitions
  remain possible. A plausible ring may also be stale or incorrectly mapped.
- Raw `opts1`, `opts2`, and `addr` fields are printed with OWN and EOR.
  RX length uses `opts1 & 0x3fff`: with OWN=1 it is advertised capacity;
  with OWN=0 it is completed receive length, not the original capacity.
  TX length uses `opts1 & 0xffff` and is not an allocation-size claim.
  Offload metadata is not fully decoded.
- Payload buffers are never dereferenced. Their ranges are calculated only.
  Eligible RAM ranges are additionally labeled `assumed-identity CPU-PA`,
  always with an unverified-translation warning.
- All ranges use `[START, END EXCLUSIVE)`. A ring range is reported only
  after EOR is observed, and remains conditional on the snapshot assumptions.

## Read-only scope and address comparisons

The application does not call network Start, Initialize, or Shutdown, reset
queues, write PCI configuration or MMIO, modify descriptors or payloads,
connect/reconnect devices, or intentionally trigger packet traffic. It does
not read clear-on-read interrupt or statistics registers. Its own allocations
and console output still affect firmware software state: read-only does not
mean that the system is frozen.

This is not a complete inventory of DMA allocations. The tool does not inspect
IOMMU page tables or intercept `PciIo.Map()`. Device-DMA ranges must be translated
independently before comparison with CPU physical addresses. Comparisons also
require the same boot and allocation state: network reinitialization or later
handoff can relocate or release rings and buffers. An overlap is a diagnostic
clue, not proof of causality; a non-overlap does not exclude unobserved DMA.

## Implementation reference

The register and descriptor reference is Linux upstream v6.12,
`drivers/net/ethernet/realtek/r8169_main.c`:

https://github.com/torvalds/linux/blob/v6.12/drivers/net/ethernet/realtek/r8169_main.c

- `enum rtl_registers` defines the ring base offsets above.
- `struct TxDesc` and `struct RxDesc` contain little-endian opts1, opts2,
  and a 64-bit address; `DescOwn` is bit31 and `RingEnd` is bit30.
- The driver selects the first MMIO BAR. This tool further restricts decoding
  to the known BAR2 layout instead of guessing a BAR for other layouts.
- `rtl8169_mark_to_asic`, `rtl_rx`, and `rtl8169_tx_map` establish the
  ownership and length interpretation used here.
- The normal upstream driver does not establish high-priority ring operation,
  so this tool does not claim to decode that queue.

This is an independent implementation using register/layout facts, not copied
Linux driver implementation code.

## Integration and validation

`NicDmaScanCommand.c` follows the existing adapter pattern and reuses the same
implementation for `UefiTools.efi nicdma`. It does not launch another EFI image
or change existing commands. The standalone application remains available.

Use the repository README's EDK II workspace and `PACKAGES_PATH` setup.
The unified and standalone INF files are both included in the package DSC:

- `Applications/UefiTools/UefiTools.inf`
- `Applications/NicDmaScan/NicDmaScan.inf`

Host tests exercise endian conversion, range overflow, RAM types/attributes
and bounds, and RX/TX length masks. They do not prove hardware DMA behavior.
Build X64 and LOONGARCH64 separately. An unsupported-NIC QEMU run can validate
launch, enumeration, and rejection, but cannot validate the RTL8168 hardware
decoder or DMA address translation.
