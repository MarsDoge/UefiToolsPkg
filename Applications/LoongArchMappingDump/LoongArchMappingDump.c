/** @file
  LoongArch64 virtual-address mapping dump utility.

  Dumps the page-table walk and current TLB entry for a specified virtual
  address. The default address is 0x0. When launched from UEFI Shell, pass a
  hexadecimal address as argv[1], for example:

    LoongArchMappingDump.efi 0x10

  Copyright (c) 2026, MarsDoge. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/ShellParameters.h>

#if defined (MDE_CPU_LOONGARCH64)

#define LA_CSR_TLBIDX      0x10
#define LA_CSR_TLBEHI      0x11
#define LA_CSR_TLBELO0     0x12
#define LA_CSR_TLBELO1     0x13
#define LA_CSR_ASID        0x18
#define LA_CSR_PGDL        0x19
#define LA_CSR_PGDH        0x1A
#define LA_CSR_PWCTL0      0x1C
#define LA_CSR_PWCTL1      0x1D
#define LA_CSR_STLBPGSIZE  0x1E
#define LA_CSR_DMWIN0      0x180
#define LA_CSR_DMWIN1      0x181
#define LA_CSR_DMWIN2      0x182
#define LA_CSR_DMWIN3      0x183

#define LA_PTE_PPN_MASK  0x0000FFFFFFFFF000ULL
#define LA_TLBIDX_NE     BIT31

STATIC
UINTN
ReadCsr (
  IN UINTN  Csr
  )
{
  UINTN  Value;

  switch (Csr) {
    case LA_CSR_TLBIDX:
      __asm__ __volatile__ ("csrrd %0, 0x10" : "=r" (Value));
      break;
    case LA_CSR_TLBEHI:
      __asm__ __volatile__ ("csrrd %0, 0x11" : "=r" (Value));
      break;
    case LA_CSR_TLBELO0:
      __asm__ __volatile__ ("csrrd %0, 0x12" : "=r" (Value));
      break;
    case LA_CSR_TLBELO1:
      __asm__ __volatile__ ("csrrd %0, 0x13" : "=r" (Value));
      break;
    case LA_CSR_ASID:
      __asm__ __volatile__ ("csrrd %0, 0x18" : "=r" (Value));
      break;
    case LA_CSR_PGDL:
      __asm__ __volatile__ ("csrrd %0, 0x19" : "=r" (Value));
      break;
    case LA_CSR_PGDH:
      __asm__ __volatile__ ("csrrd %0, 0x1a" : "=r" (Value));
      break;
    case LA_CSR_PWCTL0:
      __asm__ __volatile__ ("csrrd %0, 0x1c" : "=r" (Value));
      break;
    case LA_CSR_PWCTL1:
      __asm__ __volatile__ ("csrrd %0, 0x1d" : "=r" (Value));
      break;
    case LA_CSR_STLBPGSIZE:
      __asm__ __volatile__ ("csrrd %0, 0x1e" : "=r" (Value));
      break;
    case LA_CSR_DMWIN0:
      __asm__ __volatile__ ("csrrd %0, 0x180" : "=r" (Value));
      break;
    case LA_CSR_DMWIN1:
      __asm__ __volatile__ ("csrrd %0, 0x181" : "=r" (Value));
      break;
    case LA_CSR_DMWIN2:
      __asm__ __volatile__ ("csrrd %0, 0x182" : "=r" (Value));
      break;
    case LA_CSR_DMWIN3:
      __asm__ __volatile__ ("csrrd %0, 0x183" : "=r" (Value));
      break;
    default:
      Value = 0;
      break;
  }

  return Value;
}

STATIC
VOID
WriteTlbEHi (
  IN UINTN  Value
  )
{
  __asm__ __volatile__ ("csrwr %0, 0x11" :: "r" (Value) : "memory");
}

STATIC
VOID
TlbSearchAndRead (
  VOID
  )
{
  __asm__ __volatile__ ("tlbsrch\n\ttlbrd\n\t" ::: "memory");
}

STATIC
VOID
PrintEntryBits (
  IN CONST CHAR16  *Name,
  IN UINT64        Entry
  )
{
  Print (
    L"%s=%016lx V=%u D=%u PLV=%u C=%u G/H=%u P=%u W=%u H=%u NR=%u NX=%u RPLV=%u\r\n",
    Name,
    Entry,
    (Entry & BIT0) ? 1 : 0,
    (Entry & BIT1) ? 1 : 0,
    (UINTN)((Entry >> 2) & 0x3),
    (UINTN)((Entry >> 4) & 0x3),
    (Entry & BIT6) ? 1 : 0,
    (Entry & BIT7) ? 1 : 0,
    (Entry & BIT8) ? 1 : 0,
    (Entry & BIT12) ? 1 : 0,
    (Entry & BIT61) ? 1 : 0,
    (Entry & BIT62) ? 1 : 0,
    (Entry & BIT63) ? 1 : 0
    );
}

STATIC
UINTN
ParseMaxPageTableLevel (
  IN UINT64  PageWalkCfg
  )
{
  UINT32  Pwctl0;
  UINT32  Pwctl1;

  Pwctl0 = (UINT32)(PageWalkCfg & MAX_UINT32);
  Pwctl1 = (UINT32)((PageWalkCfg >> 32) & MAX_UINT32);

  if (((Pwctl1 >> 18) & 0x3F) != 0) {
    return 5;
  }

  if (((Pwctl1 >> 6) & 0x3F) != 0) {
    return 4;
  }

  if (((Pwctl0 >> 25) & 0x3F) != 0) {
    return 3;
  }

  return 0;
}

STATIC
UINTN
ParsePageTableBitWidth (
  IN UINT64  PageWalkCfg
  )
{
  return (UINTN)((PageWalkCfg >> 5) & 0x1F);
}

STATIC
BOOLEAN
IsTableEntry (
  IN UINT64  Entry,
  IN UINTN   Level,
  IN UINTN   MaxLevel
  )
{
  if (Entry == 0) {
    return FALSE;
  }

  if (Level >= (MaxLevel - 1)) {
    return FALSE;
  }

  if ((Entry & (BIT6 | BIT12)) == (BIT6 | BIT12)) {
    return FALSE;
  }

  return TRUE;
}

STATIC
VOID
DumpPageWalk (
  IN UINT64  Address
  )
{
  UINT64  PageWalkCfg;
  UINTN   MaxLevel;
  UINTN   Width;
  UINTN   EntryNum;
  UINTN   Root;
  UINTN   Level;
  UINTN   BlockShift;
  UINTN   Index;
  UINT64  Entry;
  UINT64  *Table;

  PageWalkCfg = LShiftU64 ((UINT64)ReadCsr (LA_CSR_PWCTL1), 32) |
                (UINT64)ReadCsr (LA_CSR_PWCTL0);
  MaxLevel = ParseMaxPageTableLevel (PageWalkCfg);
  Width    = ParsePageTableBitWidth (PageWalkCfg);

  Print (L"PageWalkCfg=%016lx MaxLevel=%lu Width=%lu\r\n", PageWalkCfg, MaxLevel, Width);
  Print (
    L"PGDL=%016lx PGDH=%016lx STLBPGSIZE=%016lx\r\n",
    (UINT64)ReadCsr (LA_CSR_PGDL),
    (UINT64)ReadCsr (LA_CSR_PGDH),
    (UINT64)ReadCsr (LA_CSR_STLBPGSIZE)
    );
  Print (
    L"DMW0=%016lx DMW1=%016lx DMW2=%016lx DMW3=%016lx\r\n",
    (UINT64)ReadCsr (LA_CSR_DMWIN0),
    (UINT64)ReadCsr (LA_CSR_DMWIN1),
    (UINT64)ReadCsr (LA_CSR_DMWIN2),
    (UINT64)ReadCsr (LA_CSR_DMWIN3)
    );

  if ((MaxLevel == 0) || (Width == 0) || (Width > 12)) {
    Print (L"Unsupported page-walk config.\r\n");
    return;
  }

  Root = ((Address & BIT47) != 0) ? ReadCsr (LA_CSR_PGDH) : ReadCsr (LA_CSR_PGDL);
  Print (L"Selected root=%016lx (%s)\r\n", (UINT64)Root, ((Address & BIT47) != 0) ? L"PGDH" : L"PGDL");

  Table    = (UINT64 *)(UINTN)(Root & LA_PTE_PPN_MASK);
  EntryNum = (UINTN)1 << Width;

  for (Level = 0; Level < MaxLevel; Level++) {
    BlockShift = (MaxLevel - Level - 1) * Width + EFI_PAGE_SHIFT;
    Index      = (UINTN)((Address >> BlockShift) & (EntryNum - 1));
    Entry      = Table[Index];

    Print (
      L"L%lu Table=%016lx Index=%lu BlockShift=%lu EntryAddr=%016lx ",
      Level,
      (UINT64)(UINTN)Table,
      Index,
      BlockShift,
      (UINT64)(UINTN)&Table[Index]
      );
    PrintEntryBits (L"Entry", Entry);

    if (!IsTableEntry (Entry, Level, MaxLevel)) {
      if (Level < (MaxLevel - 1)) {
        Print (L"Stop: leaf/huge/invalid entry at level %lu.\r\n", Level);
      }

      return;
    }

    Table = (UINT64 *)(UINTN)(Entry & LA_PTE_PPN_MASK);
  }
}

STATIC
VOID
DumpTlbEntry (
  IN UINT64  Address
  )
{
  UINTN   TlbIdx;
  UINTN   TlbEHi;
  UINTN   TlbELo0;
  UINTN   TlbELo1;
  UINTN   Asid;
  UINTN   PageShift;
  UINT64  PairMask;
  BOOLEAN CoversAddress;

  WriteTlbEHi ((UINTN)(Address & ~EFI_PAGE_MASK));
  TlbSearchAndRead ();

  TlbIdx  = ReadCsr (LA_CSR_TLBIDX);
  TlbEHi  = ReadCsr (LA_CSR_TLBEHI);
  TlbELo0 = ReadCsr (LA_CSR_TLBELO0);
  TlbELo1 = ReadCsr (LA_CSR_TLBELO1);
  Asid    = ReadCsr (LA_CSR_ASID);

  PageShift = (TlbIdx >> 24) & 0x3F;
  if ((PageShift == 0) || (PageShift > 47)) {
    PageShift = EFI_PAGE_SHIFT;
  }

  //
  // A LoongArch TLB entry carries ELO0/ELO1 for an even/odd page pair.
  // Compare TLBEHI against the pair base; if it does not cover Address,
  // tlbsrch did not find a live entry for this VA and tlbrd only exposed the
  // current TLBIDX slot.
  //
  PairMask      = ~((LShiftU64 (1, PageShift + 1)) - 1);
  CoversAddress = ((TlbEHi & PairMask) == (Address & PairMask));

  Print (
    L"TLBIDX=%016lx ASID=%016lx TLBEHI=%016lx PageShift=%lu CoversVA=%u\r\n",
    (UINT64)TlbIdx,
    (UINT64)Asid,
    (UINT64)TlbEHi,
    PageShift,
    CoversAddress ? 1 : 0
    );

  if (((TlbIdx & LA_TLBIDX_NE) != 0) || !CoversAddress) {
    Print (L"TLB search: no matching live entry for VA %016lx.\r\n", Address);
    return;
  }

  PrintEntryBits (L"TLBELO0", TlbELo0);
  PrintEntryBits (L"TLBELO1", TlbELo1);
}

STATIC
BOOLEAN
ParseHexAddress (
  IN  CONST CHAR16  *String,
  OUT UINT64        *Value
  )
{
  UINT64  Result;
  CHAR16  Ch;

  if ((String == NULL) || (Value == NULL)) {
    return FALSE;
  }

  if ((String[0] == L'0') && ((String[1] == L'x') || (String[1] == L'X'))) {
    String += 2;
  }

  Result = 0;
  while (*String != L'\0') {
    Ch = *String++;
    if ((Ch >= L'0') && (Ch <= L'9')) {
      Result = (Result << 4) | (UINT64)(Ch - L'0');
    } else if ((Ch >= L'a') && (Ch <= L'f')) {
      Result = (Result << 4) | (UINT64)(Ch - L'a' + 10);
    } else if ((Ch >= L'A') && (Ch <= L'F')) {
      Result = (Result << 4) | (UINT64)(Ch - L'A' + 10);
    } else {
      return FALSE;
    }
  }

  *Value = Result;
  return TRUE;
}

STATIC
UINT64
GetRequestedAddress (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS                     Status;
  EFI_SHELL_PARAMETERS_PROTOCOL  *ShellParameters;
  UINT64                         Address;

  Address = 0;
  Status  = gBS->HandleProtocol (
                   ImageHandle,
                   &gEfiShellParametersProtocolGuid,
                   (VOID **)&ShellParameters
                   );
  if (!EFI_ERROR (Status) && (ShellParameters->Argc > 1)) {
    if (!ParseHexAddress (ShellParameters->Argv[1], &Address)) {
      Print (L"Invalid address '%s', using 0x0.\r\n", ShellParameters->Argv[1]);
      Address = 0;
    }
  }

  return Address;
}

#endif

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
#if defined (MDE_CPU_LOONGARCH64)
  UINT64  Address;

  Address = GetRequestedAddress (ImageHandle);

  Print (L"LoongArchMappingDump: VA=%016lx\r\n", Address);
  DumpPageWalk (Address);
  DumpTlbEntry (Address);

  return EFI_SUCCESS;
#else
  Print (L"LoongArchMappingDump: this tool is only implemented for LOONGARCH64.\r\n");
  return EFI_UNSUPPORTED;
#endif
}
