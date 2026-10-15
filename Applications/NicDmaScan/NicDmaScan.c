/* SPDX-License-Identifier: Apache-2.0
   Read-only register/descriptor observer. Never enables or resets a device. */
#include <Uefi.h>
#include <IndustryStandard/Acpi.h>
#include <Protocol/PciIo.h>
#include <Protocol/SimpleNetwork.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include "Include/Decode.h"

STATIC BOOLEAN mIdentity;
STATIC UINTN mFailures;

STATIC BOOLEAN ReadConfig(EFI_PCI_IO_PROTOCOL *Pci, UINTN Offset, UINTN Count, VOID *Data) {
  return !EFI_ERROR(Pci->Pci.Read(Pci, EfiPciIoWidthUint8, Offset, Count, Data));
}

STATIC BOOLEAN PowerD0(EFI_PCI_IO_PROTOCOL *Pci, UINT8 *Cfg) {
  UINT8 Cap, Hdr[2], Pm[2];
  UINTN I;
  BOOLEAN Seen[256];
  ZeroMem(Seen, sizeof(Seen));
  if ((Cfg[6] & 0x10) == 0) { Print(L"  PCI PM capability absent\n"); return TRUE; }
  Cap = Cfg[0x34];
  for (I = 0; Cap != 0 && I < 48; I++) {
    if (Cap < 0x40 || Cap > 0xfc || (Cap & 3) != 0 || Seen[Cap]) { break; }
    Seen[Cap] = TRUE;
    if (!ReadConfig(Pci, Cap, 2, Hdr)) { break; }
    if (Hdr[0] == 1) {
      if (Cap > 0xf8 || !ReadConfig(Pci, Cap + 4, 2, Pm)) { break; }
      Print(L"  PCI power D%u\n", Pm[0] & 3);
      return (Pm[0] & 3) == 0;
    }
    Cap = Hdr[1];
  }
  if (Cap == 0) { Print(L"  PCI PM capability absent\n"); return TRUE; }
  Print(L"  Power state UNKNOWN: malformed/unreadable PCI capabilities\n");
  return FALSE;
}

STATIC BOOLEAN FindMmio(EFI_PCI_IO_PROTOCOL *Pci, UINT8 *Cfg, UINT8 *Bar) {
  UINTN I;
  UINT32 Raw;
  EFI_STATUS Status;
  VOID *Resources;
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *D;
  /* Linux r8169 uses first memory BAR. This allowlisted RTL8168 layout
     requires BAR0 I/O, unused BAR1, BAR2 64-bit memory: therefore BAR2.
     BAR4 is never substituted, regardless of its resource length. */
  if ((Le32(Cfg + 0x10) & 1) == 0 || Le32(Cfg + 0x14) != 0 ||
      (Le32(Cfg + 0x18) & 7) != 4) { return FALSE; }
  for (I = 2; I < 3; I++) {
    Raw = Le32(Cfg + 0x10 + 4 * I);
    if (Raw == 0 || Raw == 0xffffffff || (Raw & 1) != 0) { continue; }
    if ((Raw & 6) != 0 && (Raw & 6) != 4) { return FALSE; }
    Resources = NULL;
    Status = Pci->GetBarAttributes(Pci, (UINT8)I, NULL, &Resources);
    if (EFI_ERROR(Status) || Resources == NULL) { return FALSE; }
    D = Resources;
    if (D->Desc != ACPI_ADDRESS_SPACE_DESCRIPTOR || D->Len != sizeof(*D) - 3 ||
        D->ResType != ACPI_ADDRESS_SPACE_TYPE_MEM || D->AddrRangeMin == 0 ||
        D->AddrLen < 0xec) {
      FreePool(Resources);
      return FALSE;
    }
    Print(L"  MMIO BAR%u resource length=0x%lx (read via PciIo)\n", (UINT32)I, D->AddrLen);
    FreePool(Resources);
    *Bar = (UINT8)I;
    return TRUE;
  }
  return FALSE;
}

STATIC BOOLEAN ReadBase(EFI_PCI_IO_PROTOCOL *Pci, UINT8 Bar, UINT32 Offset, UINT64 *Base) {
  UINT32 A[2], B[2];
  if (EFI_ERROR(Pci->Mem.Read(Pci, EfiPciIoWidthUint32, Bar, Offset, 2, A)) ||
      EFI_ERROR(Pci->Mem.Read(Pci, EfiPciIoWidthUint32, Bar, Offset, 2, B)) ||
      CompareMem(A, B, sizeof(A)) != 0) { return FALSE; }
  *Base = (UINT64)A[0] | ((UINT64)A[1] << 32);
  return *Base != MAX_UINT64;
}

STATIC EFI_MEMORY_DESCRIPTOR *GetMap(UINTN *Size, UINTN *Stride) {
  EFI_MEMORY_DESCRIPTOR *Map;
  EFI_STATUS Status;
  UINTN Key, Capacity, Attempt;
  UINT32 Version;
  *Size = 0;
  Status = gBS->GetMemoryMap(Size, NULL, &Key, Stride, &Version);
  if (Status != EFI_BUFFER_TOO_SMALL) { return NULL; }
  for (Attempt = 0; Attempt < 3; Attempt++) {
    if (*Stride < sizeof(EFI_MEMORY_DESCRIPTOR) || *Stride > MAX_UINTN / 16 ||
        *Size > MAX_UINTN - 16 * *Stride) { return NULL; }
    Capacity = *Size + 16 * *Stride;
    Map = AllocatePool(Capacity);
    if (Map == NULL) { return NULL; }
    *Size = Capacity;
    Status = gBS->GetMemoryMap(Size, Map, &Key, Stride, &Version);
    if (!EFI_ERROR(Status)) {
      if (*Stride < sizeof(EFI_MEMORY_DESCRIPTOR) || *Size % *Stride != 0) {
        FreePool(Map); return NULL;
      }
      return Map;
    }
    FreePool(Map);
    if (Status != EFI_BUFFER_TOO_SMALL) { return NULL; }
  }
  return NULL;
}

STATIC BOOLEAN ReadableRam(EFI_MEMORY_DESCRIPTOR *Map, UINTN Size, UINTN Stride, UINT64 Base, UINT64 Length) {
  UINTN I;
  UINT64 End;
  EFI_MEMORY_DESCRIPTOR *D;
  if (!DmaEnd(Base, Length, &End) || Base > MAX_UINTN || End - 1 > MAX_UINTN) { return FALSE; }
  for (I = 0; I < Size; I += Stride) {
    D = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + I);
    if (SafeRamType(D->Type, D->Attribute) && RamContains(D->PhysicalStart, D->NumberOfPages, Base, Length)) {
      return TRUE;
    }
  }
  return FALSE;
}

STATIC VOID Snapshot(EFI_PCI_IO_PROTOCOL *Pci, UINT8 Bar, UINT32 Offset, UINT64 Base, BOOLEAN Rx) {
  EFI_MEMORY_DESCRIPTOR *Map;
  UINTN Size, Stride, I, J, Count;
  UINT8 First[16], Second[16];
  volatile UINT8 *Source;
  UINT64 Address, End, Buffer, Length, After;
  UINT32 Opts1, Opts2;
  BOOLEAN Complete;
  if (Base == 0 || (Base & 0xff) != 0) {
    Print(L"    snapshot REFUSED: zero/unaligned (256-byte) ring base\n"); return;
  }
  Map = GetMap(&Size, &Stride);
  if (Map == NULL) { Print(L"    snapshot REFUSED: memory map unavailable\n"); return; }
  Complete = FALSE;
  Count = 0;
  for (I = 0; I < SNAPSHOT_DESCRIPTORS; I++) {
    if (!DmaEnd(Base, (I + 1) * 16, &End)) { Print(L"    OVERFLOW\n"); break; }
    Address = End - 16;
    /* Validate entire observed prefix in ONE safe RAM descriptor, not MMIO. */
    if (!ReadableRam(Map, Size, Stride, Base, (I + 1) * 16)) {
      Print(L"    snapshot REFUSED at descriptor %u: prefix not in eligible RAM\n", (UINT32)I); break;
    }
    Source = (volatile UINT8 *)(UINTN)Address;
    MemoryFence();
    for (J = 0; J < 16; J++) { First[J] = Source[J]; }
    MemoryFence();
    for (J = 0; J < 16; J++) { Second[J] = Source[J]; }
    MemoryFence();
    if (CompareMem(First, Second, 16) != 0) {
      Print(L"    UNSTABLE descriptor %u; stopped\n", (UINT32)I); break;
    }
    Opts1 = Le32(First);
    Opts2 = Le32(First + 4);
    Buffer = Le64(First + 8);
    Length = RtlLength(Opts1, Rx);
    Count++;
    Print(L"    [%u] opts1=%08x opts2=%08x OWN=%u EOR=%u addr=%016lx\n",
          (UINT32)I, Opts1, Opts2, (Opts1 >> 31) & 1, (Opts1 >> 30) & 1, Buffer);
    if (Buffer != 0 && DmaEnd(Buffer, Length, &End)) {
      Print(L"      %s device-DMA=[%016lx,%016lx) len=0x%lx\n",
        Rx ? ((Opts1 & BIT31) ? L"RX advertised capacity" : L"RX completed length (NOT capacity)") :
             ((Opts1 & BIT31) ? L"TX submitted length" : L"TX stale/completed length"), Buffer, End, Length);
      if (ReadableRam(Map, Size, Stride, Buffer, Length)) {
        Print(L"      assumed-identity CPU-PA=[%016lx,%016lx) (NOT verified translation)\n", Buffer, End);
      } else {
        Print(L"      CPU-PA=UNKNOWN/not eligible RAM; payload NEVER dereferenced\n");
      }
    } else {
      Print(L"      buffer range N/A: zero address/length or overflow\n");
    }
    if ((Opts1 & BIT30) != 0) { Complete = TRUE; break; }
  }
  if (Complete && DmaEnd(Base, Count * 16, &End)) {
    Print(L"    RingEnd observed: descriptor ring device-DMA=[%016lx,%016lx) bytes=0x%lx\n",
          Base, End, (UINT64)Count * 16);
    Print(L"    assumed-identity CPU-PA=[%016lx,%016lx); NOT verified mapping\n", Base, End);
  } else {
    Print(L"    INCOMPLETE/TRUNCATED: observed=%u cap=%u; full ring length/end UNKNOWN\n",
          (UINT32)Count, SNAPSHOT_DESCRIPTORS);
  }
  if (!ReadBase(Pci, Bar, Offset, &After) || After != Base) {
    Print(L"    INVALID SNAPSHOT: ring register changed/unreadable; discard all preceding ranges\n");
  }
  FreePool(Map);
}

STATIC VOID Realtek(EFI_PCI_IO_PROTOCOL *Pci, UINT8 *Cfg) {
  UINT8 Bar;
  UINT32 Offsets[3] = { 0x20, 0x28, 0xe4 };
  CHAR16 *Names[3] = { L"TX normal", L"TX high-priority", L"RX" };
  UINTN I;
  UINT64 Base;
  if ((Cfg[4] & 2) == 0) { Print(L"  MMIO disabled: SKIPPED, not enabled by this app\n"); return; }
  if (!PowerD0(Pci, Cfg)) { Print(L"  MMIO SKIPPED: non-D0 or unknown power state\n"); return; }
  if (!FindMmio(Pci, Cfg, &Bar)) { Print(L"  MMIO SKIPPED: first memory BAR unavailable/too short\n"); return; }
  for (I = 0; I < 3; I++) {
    if (!ReadBase(Pci, Bar, Offsets[I], &Base)) {
      Print(L"  %s base N/A: read failed/unstable/all-ones\n", Names[I]); mFailures++; continue;
    }
    Print(L"  %s device-DMA base=%016lx length=UNKNOWN end=UNKNOWN CPU-PA=UNKNOWN\n", Names[I], Base);
    if (I == 1) {
      Print(L"    High-priority registers only: activation/layout not established; no descriptor reads\n");
    } else if (mIdentity) {
      Snapshot(Pci, Bar, Offsets[I], Base, I == 2);
    } else {
      Print(L"    Payload N/A: no DMA-to-CPU translation; descriptors not read\n");
    }
  }
}

INTN EFIAPI ShellAppMain(UINTN Argc, CHAR16 **Argv) {
  EFI_STATUS Status;
  EFI_HANDLE *Handles;
  EFI_PCI_IO_PROTOCOL *Pci;
  EFI_SIMPLE_NETWORK_PROTOCOL *Snp;
  UINTN Count, I, Segment, Bus, Device, Function, Networks;
  UINT8 Cfg[64];
  UINT32 Id;
  mIdentity = FALSE;
  mFailures = 0;
  if (Argc == 2 && StrCmp(Argv[1], L"--identity-map") == 0) {
    mIdentity = TRUE;
  } else if (Argc != 1) {
    Print(L"Usage: NicDmaScan.efi [--identity-map]\n");
    Print(L"   or: UefiTools.efi nicdma [--identity-map]\n");
    Print(L"Default: config + allowlisted read-only MMIO. No device state changes.\n");
    Print(L"--identity-map asserts device DMA == CPU physical == CPU pointer, coherent RAM.\n");
    Print(L"Do NOT use with IOMMU/bus translation/non-identity or noncoherent mappings.\n");
    return (Argc == 2 && (StrCmp(Argv[1], L"--help") == 0 || StrCmp(Argv[1], L"-h") == 0)) ? 0 : 2;
  }
  Print(L"NicDmaScan: READ-ONLY firmware-visible PCI network inventory\n");
  Print(L"Ranges are [START, END EXCLUSIVE). Device-DMA != proven host physical.\n");
  Print(L"Identity descriptor reads: %s\n", mIdentity ? L"USER ASSERTED (NOT verified)" : L"OFF");
  Print(L"No Start/Initialize/Shutdown/reset/config writes. Snapshot is NOT all DMA allocations.\n");
  Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiPciIoProtocolGuid, NULL, &Count, &Handles);
  if (Status == EFI_NOT_FOUND) { Print(L"No firmware-visible PCI handles.\n"); return 0; }
  if (EFI_ERROR(Status)) { Print(L"PCI enumeration failed: %r\n", Status); return 1; }
  Networks = 0;
  for (I = 0; I < Count; I++) {
    Status = gBS->HandleProtocol(Handles[I], &gEfiPciIoProtocolGuid, (VOID **)&Pci);
    if (EFI_ERROR(Status) || !ReadConfig(Pci, 0, sizeof(Cfg), Cfg)) { mFailures++; continue; }
    if (Cfg[11] != 2) { continue; }
    Networks++;
    Id = Le32(Cfg);
    Status = Pci->GetLocation(Pci, &Segment, &Bus, &Device, &Function);
    if (!EFI_ERROR(Status)) {
      Print(L"\n%04x:%02x:%02x.%x ", (UINT32)Segment, (UINT32)Bus, (UINT32)Device, (UINT32)Function);
    } else { Print(L"\nBDF=UNKNOWN "); }
    Print(L"%04x:%04x rev=%02x class=02:%02x:%02x command=%04x BME=%u\n",
          Id & 0xffff, Id >> 16, Cfg[8], Cfg[10], Cfg[9], (UINT32)(Cfg[4] | (Cfg[5] << 8)), (Cfg[4] >> 2) & 1);
    Status = gBS->HandleProtocol(Handles[I], &gEfiSimpleNetworkProtocolGuid, (VOID **)&Snp);
    if (!EFI_ERROR(Status) && Snp->Mode != NULL) {
      Print(L"  Same-handle SNP state=%u (0=Stopped 1=Started 2=Initialized)\n", Snp->Mode->State);
    } else { Print(L"  SNP on same handle N/A (child handles not correlated)\n"); }
    if (Id == 0x816810ec && Cfg[8] == 0x15 && (Cfg[0x0e] & 0x7f) == 0) {
      Print(L"  Decoder: Realtek 10ec:8168 PCI rev 15; exact MAC version UNKNOWN\n");
      Realtek(Pci, Cfg);
    } else {
      Print(L"  DMA decoder N/A (allowlist: 10ec:8168 PCI rev 15 only); no MMIO read\n");
    }
  }
  FreePool(Handles);
  Print(L"\nNetwork handles=%u; access failures=%u. Completeness: PARTIAL, never all DMA.\n", (UINT32)Networks, (UINT32)mFailures);
  Print(L"Use same-boot evidence only. Reinitialization/launch/reset can relocate rings.\n");
  Print(L"Direct memtest comparison requires independently verified CPU physical translation.\n");
  return mFailures == 0 ? 0 : 1;
}
