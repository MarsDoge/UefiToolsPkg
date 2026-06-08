/** @file
  Print PCI/PCIe topology from EFI_PCI_IO_PROTOCOL handles.

  This shell application builds a firmware-visible PCI topology tree similar to
  the high-level relationship view from Linux `lspci -tv`. It uses the devices
  already enumerated by firmware, identifies PCI-to-PCI bridges from config
  space, and nests downstream devices by bridge secondary/subordinate bus range.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <IndustryStandard/Pci22.h>
#include <Protocol/PciIo.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#define PCI_TOPO_NO_PARENT  MAX_UINTN

typedef struct {
  EFI_HANDLE           Handle;
  EFI_PCI_IO_PROTOCOL  *PciIo;
  PCI_TYPE_GENERIC     Pci;
  UINTN                Segment;
  UINTN                Bus;
  UINTN                Device;
  UINTN                Function;
  BOOLEAN              IsBridge;
  UINT8                PrimaryBus;
  UINT8                SecondaryBus;
  UINT8                SubordinateBus;
  UINTN                Parent;
  BOOLEAN              Printed;
} PCI_TOPO_DEVICE;

STATIC
CONST CHAR16 *
ClassName (
  IN UINT8  BaseClass,
  IN UINT8  SubClass,
  IN UINT8  ProgIf
  )
{
  switch (BaseClass) {
    case PCI_CLASS_OLD:
      return L"Old/legacy";
    case PCI_CLASS_MASS_STORAGE:
      return L"Mass storage";
    case PCI_CLASS_NETWORK:
      return L"Network";
    case PCI_CLASS_DISPLAY:
      return L"Display";
    case PCI_CLASS_MEDIA:
      return L"Multimedia";
    case PCI_CLASS_MEMORY_CONTROLLER:
      return L"Memory";
    case PCI_CLASS_BRIDGE:
      switch (SubClass) {
        case PCI_CLASS_BRIDGE_HOST:
          return L"Host bridge";
        case PCI_CLASS_BRIDGE_ISA:
          return L"ISA bridge";
        case PCI_CLASS_BRIDGE_P2P:
          return (ProgIf == PCI_IF_BRIDGE_P2P_SUBTRACTIVE) ? L"PCI bridge (subtract)" : L"PCI bridge";
        default:
          return L"Bridge";
      }
    case PCI_CLASS_SERIAL:
      return L"Serial bus";
    case PCI_CLASS_WIRELESS:
      return L"Wireless";
    case PCI_CLASS_INTELLIGENT_IO:
      return L"Intelligent I/O";
    case PCI_CLASS_SATELLITE:
      return L"Satellite";
    case PCI_CLASS_DPIO:
      return L"Signal processing";
    default:
      return L"Device";
  }
}

STATIC
BOOLEAN
IsPciToPciBridge (
  IN CONST PCI_TYPE_GENERIC  *Pci
  )
{
  return (BOOLEAN)(
                    (Pci->Device.Hdr.ClassCode[2] == PCI_CLASS_BRIDGE) &&
                    (Pci->Device.Hdr.ClassCode[1] == PCI_CLASS_BRIDGE_P2P)
                    );
}

STATIC
INTN
CompareDevice (
  IN CONST PCI_TOPO_DEVICE  *Left,
  IN CONST PCI_TOPO_DEVICE  *Right
  )
{
  if (Left->Segment != Right->Segment) {
    return (Left->Segment < Right->Segment) ? -1 : 1;
  }

  if (Left->Bus != Right->Bus) {
    return (Left->Bus < Right->Bus) ? -1 : 1;
  }

  if (Left->Device != Right->Device) {
    return (Left->Device < Right->Device) ? -1 : 1;
  }

  if (Left->Function != Right->Function) {
    return (Left->Function < Right->Function) ? -1 : 1;
  }

  return 0;
}

STATIC
VOID
SortDevices (
  IN OUT PCI_TOPO_DEVICE  *Devices,
  IN     UINTN            Count
  )
{
  UINTN            Index;
  UINTN            Insert;
  PCI_TOPO_DEVICE  Current;

  for (Index = 1; Index < Count; Index++) {
    CopyMem (&Current, &Devices[Index], sizeof (Current));
    Insert = Index;
    while ((Insert > 0) && (CompareDevice (&Current, &Devices[Insert - 1]) < 0)) {
      CopyMem (&Devices[Insert], &Devices[Insert - 1], sizeof (Devices[Insert]));
      Insert--;
    }

    CopyMem (&Devices[Insert], &Current, sizeof (Devices[Insert]));
  }
}

STATIC
VOID
AssignParents (
  IN OUT PCI_TOPO_DEVICE  *Devices,
  IN     UINTN            Count
  )
{
  UINTN  Child;
  UINTN  Bridge;
  UINTN  Best;
  UINTN  Span;
  UINTN  BestSpan;

  for (Child = 0; Child < Count; Child++) {
    Devices[Child].Parent = PCI_TOPO_NO_PARENT;
    Best                  = PCI_TOPO_NO_PARENT;
    BestSpan              = MAX_UINTN;

    for (Bridge = 0; Bridge < Count; Bridge++) {
      if ((Bridge == Child) || !Devices[Bridge].IsBridge) {
        continue;
      }

      if (Devices[Bridge].Segment != Devices[Child].Segment) {
        continue;
      }

      if (Devices[Bridge].SecondaryBus > Devices[Bridge].SubordinateBus) {
        continue;
      }

      if ((Devices[Child].Bus < Devices[Bridge].SecondaryBus) ||
          (Devices[Child].Bus > Devices[Bridge].SubordinateBus))
      {
        continue;
      }

      Span = (UINTN)Devices[Bridge].SubordinateBus - (UINTN)Devices[Bridge].SecondaryBus;
      if (Span < BestSpan) {
        Best     = Bridge;
        BestSpan = Span;
      }
    }

    Devices[Child].Parent = Best;
  }
}

STATIC
BOOLEAN
HasChild (
  IN PCI_TOPO_DEVICE  *Devices,
  IN UINTN            Count,
  IN UINTN            Parent
  )
{
  UINTN  Index;

  for (Index = 0; Index < Count; Index++) {
    if (Devices[Index].Parent == Parent) {
      return TRUE;
    }
  }

  return FALSE;
}

STATIC
UINTN
LastChildIndex (
  IN PCI_TOPO_DEVICE  *Devices,
  IN UINTN            Count,
  IN UINTN            Parent
  )
{
  UINTN  Index;
  UINTN  Last;

  Last = PCI_TOPO_NO_PARENT;
  for (Index = 0; Index < Count; Index++) {
    if (Devices[Index].Parent == Parent) {
      Last = Index;
    }
  }

  return Last;
}

STATIC
UINTN
LastRootInSegment (
  IN PCI_TOPO_DEVICE  *Devices,
  IN UINTN            Count,
  IN UINTN            Segment
  )
{
  UINTN  Index;
  UINTN  Last;

  Last = PCI_TOPO_NO_PARENT;
  for (Index = 0; Index < Count; Index++) {
    if ((Devices[Index].Segment == Segment) && (Devices[Index].Parent == PCI_TOPO_NO_PARENT)) {
      Last = Index;
    }
  }

  return Last;
}

STATIC
VOID
PrintPrefix (
  IN UINTN    Depth,
  IN BOOLEAN  *LastAtDepth,
  IN BOOLEAN  IsLast
  )
{
  UINTN  Index;

  for (Index = 0; Index < Depth; Index++) {
    Print (LastAtDepth[Index] ? L"   " : L"|  ");
  }

  Print (IsLast ? L"`- " : L"+- ");
}

STATIC
VOID
PrintDeviceLine (
  IN PCI_TOPO_DEVICE  *Device
  )
{
  UINT8  BaseClass;
  UINT8  SubClass;
  UINT8  ProgIf;

  BaseClass = Device->Pci.Device.Hdr.ClassCode[2];
  SubClass  = Device->Pci.Device.Hdr.ClassCode[1];
  ProgIf    = Device->Pci.Device.Hdr.ClassCode[0];

  Print (
    L"%02x:%02x.%x [%02x%02x%02x] %s %04x:%04x",
    (UINT32)Device->Bus,
    (UINT32)Device->Device,
    (UINT32)Device->Function,
    BaseClass,
    SubClass,
    ProgIf,
    ClassName (BaseClass, SubClass, ProgIf),
    Device->Pci.Device.Hdr.VendorId,
    Device->Pci.Device.Hdr.DeviceId
    );

  if (Device->IsBridge) {
    Print (
      L" -> bus %02x-%02x primary=%02x",
      Device->SecondaryBus,
      Device->SubordinateBus,
      Device->PrimaryBus
      );
  }

  Print (L"\n");
}

STATIC
VOID
PrintSubtree (
  IN OUT PCI_TOPO_DEVICE  *Devices,
  IN     UINTN            Count,
  IN     UINTN            Index,
  IN     UINTN            Depth,
  IN OUT BOOLEAN          *LastAtDepth,
  IN     BOOLEAN          IsLast
  )
{
  UINTN  Child;
  UINTN  LastChild;

  PrintPrefix (Depth, LastAtDepth, IsLast);
  PrintDeviceLine (&Devices[Index]);
  Devices[Index].Printed = TRUE;

  if (!HasChild (Devices, Count, Index)) {
    return;
  }

  if (Depth + 1 >= 32) {
    Print (L"   <maximum tree depth reached>\n");
    return;
  }

  LastAtDepth[Depth] = IsLast;
  LastChild          = LastChildIndex (Devices, Count, Index);

  for (Child = 0; Child < Count; Child++) {
    if (Devices[Child].Parent == Index) {
      PrintSubtree (Devices, Count, Child, Depth + 1, LastAtDepth, (BOOLEAN)(Child == LastChild));
    }
  }
}

STATIC
EFI_STATUS
CollectDevices (
  OUT PCI_TOPO_DEVICE  **DevicesOut,
  OUT UINTN            *CountOut
  )
{
  EFI_STATUS           Status;
  EFI_HANDLE           *Handles;
  UINTN                HandleCount;
  UINTN                Index;
  UINTN                Count;
  PCI_TOPO_DEVICE      *Devices;
  EFI_PCI_IO_PROTOCOL  *PciIo;

  *DevicesOut = NULL;
  *CountOut   = 0;
  Handles     = NULL;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiPciIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Devices = AllocateZeroPool (sizeof (*Devices) * HandleCount);
  if (Devices == NULL) {
    FreePool (Handles);
    return EFI_OUT_OF_RESOURCES;
  }

  Count = 0;
  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (
                    Handles[Index],
                    &gEfiPciIoProtocolGuid,
                    (VOID **)&PciIo
                    );
    if (EFI_ERROR (Status)) {
      continue;
    }

    ZeroMem (&Devices[Count].Pci, sizeof (Devices[Count].Pci));
    Status = PciIo->Pci.Read (
                          PciIo,
                          EfiPciIoWidthUint8,
                          0,
                          sizeof (PCI_TYPE_GENERIC),
                          &Devices[Count].Pci
                          );
    if (EFI_ERROR (Status)) {
      Print (L"PCI config read failed for handle[%u]: %r\n", (UINT32)Index, Status);
      continue;
    }

    Status = PciIo->GetLocation (
                      PciIo,
                      &Devices[Count].Segment,
                      &Devices[Count].Bus,
                      &Devices[Count].Device,
                      &Devices[Count].Function
                      );
    if (EFI_ERROR (Status)) {
      Print (L"PCI GetLocation failed for handle[%u]: %r\n", (UINT32)Index, Status);
      continue;
    }

    Devices[Count].Handle = Handles[Index];
    Devices[Count].PciIo  = PciIo;
    Devices[Count].Parent = PCI_TOPO_NO_PARENT;

    Devices[Count].IsBridge = IsPciToPciBridge (&Devices[Count].Pci);
    if (Devices[Count].IsBridge) {
      Devices[Count].PrimaryBus     = Devices[Count].Pci.Bridge.Bridge.PrimaryBus;
      Devices[Count].SecondaryBus   = Devices[Count].Pci.Bridge.Bridge.SecondaryBus;
      Devices[Count].SubordinateBus = Devices[Count].Pci.Bridge.Bridge.SubordinateBus;
    }

    Count++;
  }

  FreePool (Handles);

  if (Count == 0) {
    FreePool (Devices);
    return EFI_NOT_FOUND;
  }

  *DevicesOut = Devices;
  *CountOut   = Count;
  return EFI_SUCCESS;
}

STATIC
VOID
PrintTopology (
  IN OUT PCI_TOPO_DEVICE  *Devices,
  IN     UINTN            Count
  )
{
  UINTN    Index;
  UINTN    LastRoot;
  UINTN    CurrentSegment;
  BOOLEAN  LastAtDepth[32];

  ZeroMem (LastAtDepth, sizeof (LastAtDepth));

  CurrentSegment = MAX_UINTN;
  LastRoot       = PCI_TOPO_NO_PARENT;

  for (Index = 0; Index < Count; Index++) {
    if (Devices[Index].Segment != CurrentSegment) {
      CurrentSegment = Devices[Index].Segment;
      LastRoot       = LastRootInSegment (Devices, Count, CurrentSegment);
      Print (L"\nDomain %04x\n", (UINT32)CurrentSegment);
    }

    if (Devices[Index].Parent == PCI_TOPO_NO_PARENT) {
      PrintSubtree (Devices, Count, Index, 0, LastAtDepth, (BOOLEAN)(Index == LastRoot));
    }
  }

  for (Index = 0; Index < Count; Index++) {
    if (!Devices[Index].Printed) {
      Print (L"\nUnprinted/orphan device (possible unusual bridge bus range):\n");
      PrintDeviceLine (&Devices[Index]);
    }
  }
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS       Status;
  PCI_TOPO_DEVICE  *Devices;
  UINTN            Count;
  UINTN            BridgeCount;
  UINTN            Index;

  Devices = NULL;
  Count   = 0;

  Print (L"PCI Topology (EFI_PCI_IO_PROTOCOL scan)\n");
  Print (L"Firmware-visible topology; devices skipped by PCI enumeration will not appear.\n");

  Status = CollectDevices (&Devices, &Count);
  if (EFI_ERROR (Status)) {
    Print (L"No PCI I/O handles found: %r\n", Status);
    return Status;
  }

  SortDevices (Devices, Count);
  AssignParents (Devices, Count);

  BridgeCount = 0;
  for (Index = 0; Index < Count; Index++) {
    if (Devices[Index].IsBridge) {
      BridgeCount++;
    }
  }

  Print (L"Devices=%u Bridges=%u\n", (UINT32)Count, (UINT32)BridgeCount);
  PrintTopology (Devices, Count);

  FreePool (Devices);
  return EFI_SUCCESS;
}
