/** @file
  Print PCI Option ROM information for devices exposed through EFI_PCI_IO_PROTOCOL.

  The parser walks every image in a PCI Option ROM and prints the ROM size,
  PCIR metadata, code type, and EFI image machine type when present.

  SPDX-License-Identifier: Apache-2.0
**/

#include <Uefi.h>

#include <IndustryStandard/Pci22.h>
#include <Protocol/PciIo.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

STATIC
BOOLEAN
RangeInBuffer (
  IN UINTN  BufferSize,
  IN UINTN  Offset,
  IN UINTN  Length
  )
{
  if (Offset > BufferSize) {
    return FALSE;
  }

  if (Length > BufferSize - Offset) {
    return FALSE;
  }

  return TRUE;
}

STATIC
CONST CHAR16 *
CodeTypeName (
  IN UINT8  CodeType
  )
{
  switch (CodeType) {
    case PCI_CODE_TYPE_PCAT_IMAGE:
      return L"PC-AT/Legacy";
    case 0x03:
      return L"EFI";
    default:
      return L"Unknown";
  }
}

STATIC
CONST CHAR16 *
MachineTypeName (
  IN UINT16  MachineType
  )
{
  switch (MachineType) {
    case EFI_IMAGE_MACHINE_IA32:
      return L"IA32";
    case EFI_IMAGE_MACHINE_X64:
      return L"X64";
    case EFI_IMAGE_MACHINE_IA64:
      return L"IA64";
    case EFI_IMAGE_MACHINE_EBC:
      return L"EBC";
    case EFI_IMAGE_MACHINE_AARCH64:
      return L"AARCH64";
    case EFI_IMAGE_MACHINE_RISCV32:
      return L"RISCV32";
    case EFI_IMAGE_MACHINE_RISCV64:
      return L"RISCV64";
    case EFI_IMAGE_MACHINE_RISCV128:
      return L"RISCV128";
    case EFI_IMAGE_MACHINE_LOONGARCH32:
      return L"LOONGARCH32";
    case EFI_IMAGE_MACHINE_LOONGARCH64:
      return L"LOONGARCH64";
    default:
      return L"Unknown";
  }
}

STATIC
VOID
PrintEfiRomHeader (
  IN CONST UINT8                *RomBuffer,
  IN UINTN                     RomSize,
  IN UINTN                     ImageOffset,
  IN UINTN                     ImageSize,
  IN CONST PCI_DATA_STRUCTURE  *Pcir
  )
{
  EFI_PCI_EXPANSION_ROM_HEADER  EfiRomHeader;

  if (Pcir->CodeType != 0x03) {
    return;
  }

  if (!RangeInBuffer (RomSize, ImageOffset, sizeof (EfiRomHeader)) ||
      (ImageSize < sizeof (EfiRomHeader))) {
    Print (L"      EFI Header: truncated\n");
    return;
  }

  CopyMem (&EfiRomHeader, RomBuffer + ImageOffset, sizeof (EfiRomHeader));

  if (EfiRomHeader.EfiSignature != EFI_PCI_EXPANSION_ROM_HEADER_EFISIGNATURE) {
    Print (
      L"      EFI Header: invalid signature 0x%04x\n",
      EfiRomHeader.EfiSignature
      );
    return;
  }

  if (EfiRomHeader.EfiImageHeaderOffset > ImageSize) {
    Print (
      L"      EFI ImageHeaderOffset: 0x%04x (outside image)\n",
      EfiRomHeader.EfiImageHeaderOffset
      );
  }

  Print (
    L"      EFI Arch: %s (MachineType=0x%04x)\n",
    MachineTypeName (EfiRomHeader.EfiMachineType),
    EfiRomHeader.EfiMachineType
    );
  Print (
    L"      EFI Subsystem: 0x%04x  Compression: 0x%04x  InitSize: %u bytes  ImageHeaderOffset: 0x%04x\n",
    EfiRomHeader.EfiSubsystem,
    EfiRomHeader.CompressionType,
    (UINT32)EfiRomHeader.InitializationSize * 512U,
    EfiRomHeader.EfiImageHeaderOffset
    );
}

STATIC
VOID
PrintOptionRomImages (
  IN VOID    *RomImage,
  IN UINT64  RomSize64
  )
{
  CONST UINT8               *RomBuffer;
  UINTN                    RomSize;
  UINTN                    Offset;
  UINTN                    ImageSize;
  UINTN                    PcirOffset;
  UINTN                    ImageIndex;
  PCI_EXPANSION_ROM_HEADER  RomHeader;
  PCI_DATA_STRUCTURE        Pcir;

  if ((RomImage == NULL) || (RomSize64 == 0)) {
    return;
  }

  if (RomSize64 > MAX_UINTN) {
    Print (L"    ROM too large for this build: %Lu bytes\n", RomSize64);
    return;
  }

  RomBuffer  = (CONST UINT8 *)RomImage;
  RomSize    = (UINTN)RomSize64;
  Offset     = 0;
  ImageIndex = 0;

  while (Offset < RomSize) {
    if (!RangeInBuffer (RomSize, Offset, sizeof (RomHeader))) {
      Print (L"    Image[%u] @0x%lx: truncated ROM header\n", ImageIndex, Offset);
      break;
    }

    CopyMem (&RomHeader, RomBuffer + Offset, sizeof (RomHeader));
    if (RomHeader.Signature != PCI_EXPANSION_ROM_HEADER_SIGNATURE) {
      Print (
        L"    Image[%u] @0x%lx: invalid ROM signature 0x%04x\n",
        ImageIndex,
        Offset,
        RomHeader.Signature
        );
      break;
    }

    if (RomHeader.PcirOffset > RomSize - Offset) {
      Print (
        L"    Image[%u] @0x%lx: PCIR offset 0x%04x outside ROM\n",
        ImageIndex,
        Offset,
        RomHeader.PcirOffset
        );
      break;
    }

    PcirOffset = Offset + RomHeader.PcirOffset;
    if (!RangeInBuffer (RomSize, PcirOffset, sizeof (Pcir))) {
      Print (
        L"    Image[%u] @0x%lx: truncated PCIR at 0x%lx\n",
        ImageIndex,
        Offset,
        PcirOffset
        );
      break;
    }

    CopyMem (&Pcir, RomBuffer + PcirOffset, sizeof (Pcir));
    if (Pcir.Signature != PCI_DATA_STRUCTURE_SIGNATURE) {
      Print (
        L"    Image[%u] @0x%lx: invalid PCIR signature 0x%08x\n",
        ImageIndex,
        Offset,
        Pcir.Signature
        );
      break;
    }

    ImageSize = (UINTN)Pcir.ImageLength * 512U;
    Print (
      L"    Image[%u] Offset=0x%lx Size=%u bytes CodeType=0x%02x (%s) Indicator=0x%02x%s\n",
      ImageIndex,
      Offset,
      (UINT32)ImageSize,
      Pcir.CodeType,
      CodeTypeName (Pcir.CodeType),
      Pcir.Indicator,
      ((Pcir.Indicator & 0x80) != 0) ? L" Last" : L""
      );
    Print (
      L"      PCIR Vendor=0x%04x Device=0x%04x Revision=0x%02x CodeRevision=0x%04x Class=%02x%02x%02x StructLength=%u\n",
      Pcir.VendorId,
      Pcir.DeviceId,
      Pcir.Revision,
      Pcir.CodeRevision,
      Pcir.ClassCode[2],
      Pcir.ClassCode[1],
      Pcir.ClassCode[0],
      Pcir.Length
      );

    if ((ImageSize == 0) || (ImageSize > RomSize - Offset)) {
      Print (L"      Image length is invalid for remaining ROM buffer\n");
      break;
    }

    PrintEfiRomHeader (RomBuffer, RomSize, Offset, ImageSize, &Pcir);

    if ((Pcir.Indicator & 0x80) != 0) {
      break;
    }

    Offset += ImageSize;
    ImageIndex++;
  }
}

STATIC
EFI_STATUS
PrintDeviceRomInfo (
  IN EFI_HANDLE  Handle
  )
{
  EFI_STATUS           Status;
  EFI_PCI_IO_PROTOCOL  *PciIo;
  PCI_TYPE_GENERIC     Pci;
  UINTN                Segment;
  UINTN                Bus;
  UINTN                Device;
  UINTN                Function;

  Status = gBS->HandleProtocol (
                  Handle,
                  &gEfiPciIoProtocolGuid,
                  (VOID **)&PciIo
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((PciIo->RomImage == NULL) || (PciIo->RomSize == 0)) {
    return EFI_NOT_FOUND;
  }

  ZeroMem (&Pci, sizeof (Pci));
  Status = PciIo->Pci.Read (
                        PciIo,
                        EfiPciIoWidthUint8,
                        0,
                        sizeof (PCI_DEVICE_INDEPENDENT_REGION),
                        &Pci
                        );
  if (EFI_ERROR (Status)) {
    Print (L"PCI device: config read failed: %r\n", Status);
    return Status;
  }

  Status = PciIo->GetLocation (PciIo, &Segment, &Bus, &Device, &Function);
  if (EFI_ERROR (Status)) {
    Segment  = 0;
    Bus      = 0;
    Device   = 0;
    Function = 0;
  }

  Print (
    L"PCI %u:%02x:%02x.%x Vendor=0x%04x Device=0x%04x Class=%02x%02x%02x Header=0x%02x\n",
    Segment,
    Bus,
    Device,
    Function,
    Pci.Device.Hdr.VendorId,
    Pci.Device.Hdr.DeviceId,
    Pci.Device.Hdr.ClassCode[2],
    Pci.Device.Hdr.ClassCode[1],
    Pci.Device.Hdr.ClassCode[0],
    Pci.Device.Hdr.HeaderType
    );
  Print (
    L"  Option ROM: RomImage=0x%p RomSize=%Lu bytes\n",
    PciIo->RomImage,
    PciIo->RomSize
    );

  PrintOptionRomImages (PciIo->RomImage, PciIo->RomSize);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles;
  UINTN       HandleCount;
  UINTN       Index;
  UINTN       RomDeviceCount;

  Handles        = NULL;
  HandleCount    = 0;
  RomDeviceCount = 0;

  Print (L"PCI Option ROM Info (EFI_PCI_IO_PROTOCOL scan)\n");

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiPciIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    Print (L"No PCI I/O handles found: %r\n", Status);
    return Status;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    Status = PrintDeviceRomInfo (Handles[Index]);
    if (!EFI_ERROR (Status)) {
      RomDeviceCount++;
    }
  }

  if (Handles != NULL) {
    FreePool (Handles);
  }

  Print (
    L"Done. PCI handles scanned=%u, devices with Option ROM=%u\n",
    (UINT32)HandleCount,
    (UINT32)RomDeviceCount
    );

  return EFI_SUCCESS;
}
