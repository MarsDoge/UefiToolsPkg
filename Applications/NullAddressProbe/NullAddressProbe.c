/** @file
  Probe whether virtual address 0 is readable in the current UEFI mapping.

  This diagnostic intentionally performs a real architecture load from address
  zero. If null-pointer detection is implemented by the firmware page tables,
  the load should raise a CPU exception before the success line is printed.

  Copyright (c) 2026, MarsDoge. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/UefiLib.h>

#if defined (MDE_CPU_LOONGARCH64)
#define LA_PPN_MASK 0x0000FFFFFFFFF000ULL

STATIC UINTN ReadCsrPgdl (VOID) { UINTN Value; __asm__ __volatile__ ("csrrd %0, 0x19" : "=r" (Value)); return Value; }
STATIC UINTN ReadCsrPwctl0 (VOID) { UINTN Value; __asm__ __volatile__ ("csrrd %0, 0x1c" : "=r" (Value)); return Value; }
STATIC UINTN ReadCsrPwctl1 (VOID) { UINTN Value; __asm__ __volatile__ ("csrrd %0, 0x1d" : "=r" (Value)); return Value; }
STATIC UINTN ReadCsrTlbIdx (VOID) { UINTN Value; __asm__ __volatile__ ("csrrd %0, 0x10" : "=r" (Value)); return Value; }
STATIC UINTN ReadCsrTlbEHi (VOID) { UINTN Value; __asm__ __volatile__ ("csrrd %0, 0x11" : "=r" (Value)); return Value; }
STATIC UINTN ReadCsrTlbELo0 (VOID) { UINTN Value; __asm__ __volatile__ ("csrrd %0, 0x12" : "=r" (Value)); return Value; }
STATIC UINTN ReadCsrTlbELo1 (VOID) { UINTN Value; __asm__ __volatile__ ("csrrd %0, 0x13" : "=r" (Value)); return Value; }
STATIC UINTN ReadCsrAsid (VOID) { UINTN Value; __asm__ __volatile__ ("csrrd %0, 0x18" : "=r" (Value)); return Value; }

STATIC VOID PrintBits (IN CONST CHAR16 *Name, IN UINT64 Entry) {
  Print (L"NullAddressProbe: %s=%016lx V=%u D=%u G=%u P=%u W=%u H=%u NR=%u NX=%u\r\n", Name, Entry,
    (Entry & BIT0) ? 1 : 0, (Entry & BIT1) ? 1 : 0, (Entry & BIT6) ? 1 : 0,
    (Entry & BIT7) ? 1 : 0, (Entry & BIT8) ? 1 : 0, (Entry & BIT12) ? 1 : 0,
    (Entry & BIT61) ? 1 : 0, (Entry & BIT62) ? 1 : 0);
}

STATIC UINTN Follow0 (IN CONST CHAR16 *Name, IN UINTN Base) {
  UINT64 Entry = ((volatile UINT64 *)(UINTN)(Base & LA_PPN_MASK))[0];
  PrintBits (Name, Entry);
  return (UINTN)(Entry & LA_PPN_MASK);
}

STATIC VOID DumpVa0PageTable (VOID) {
  UINTN Base = ReadCsrPgdl ();
  UINTN Pw0 = ReadCsrPwctl0 ();
  UINTN Pw1 = ReadCsrPwctl1 ();
  UINTN Dir3Width = (Pw1 >> 6) & 0x3F;
  UINTN Dir4Width = (Pw1 >> 18) & 0x3F;
  UINT64 Pte0;
  UINT64 Pte1;

  Print (L"NullAddressProbe: PGDL=%016lx PWCTL0=%016lx PWCTL1=%016lx Dir3Width=%lu Dir4Width=%lu\r\n", (UINT64)Base, (UINT64)Pw0, (UINT64)Pw1, (UINT64)Dir3Width, (UINT64)Dir4Width);
  if (Dir4Width != 0) { Base = Follow0 (L"LDDIR4[0]", Base); }
  if (Dir3Width != 0) { Base = Follow0 (L"LDDIR3[0]", Base); }
  Base = Follow0 (L"LDDIR2[0]", Base);
  Base = Follow0 (L"LDDIR1[0]", Base);
  Pte0 = ((volatile UINT64 *)(UINTN)(Base & LA_PPN_MASK))[0];
  Pte1 = ((volatile UINT64 *)(UINTN)(Base & LA_PPN_MASK))[1];
  PrintBits (L"PTE0[VA0]", Pte0);
  PrintBits (L"PTE1[VA1000]", Pte1);
}

STATIC VOID DumpVa0TlbEntry (VOID) {
  UINTN TlbIdx;
  UINTN TlbEHi;
  UINTN TlbELo0;
  UINTN TlbELo1;
  UINTN Asid;

  __asm__ __volatile__ ("csrwr $zero, 0x11\n\ttlbsrch\n\ttlbrd\n\t" ::: "memory");
  TlbIdx = ReadCsrTlbIdx ();
  TlbEHi = ReadCsrTlbEHi ();
  TlbELo0 = ReadCsrTlbELo0 ();
  TlbELo1 = ReadCsrTlbELo1 ();
  Asid = ReadCsrAsid ();

  Print (L"NullAddressProbe: TLBSRCH VA0 TLBIDX=%016lx ASID=%016lx TLBEHI=%016lx TLBELO0=%016lx TLBELO1=%016lx\r\n", (UINT64)TlbIdx, (UINT64)Asid, (UINT64)TlbEHi, (UINT64)TlbELo0, (UINT64)TlbELo1);
  PrintBits (L"TLBELO0", TlbELo0);
  PrintBits (L"TLBELO1", TlbELo1);
}
#endif


STATIC
UINTN
ReadNullAddress (
  VOID
  )
{
  UINTN  Value;

#if defined (MDE_CPU_X64)
  __asm__ __volatile__ (
    "xor %%rax, %%rax\n\t"
    "movq (%%rax), %%rax\n\t"
    "movq %%rax, %0\n\t"
    : "=r" (Value)
    :
    : "rax", "memory"
    );
#elif defined (MDE_CPU_IA32)
  __asm__ __volatile__ (
    "xorl %%eax, %%eax\n\t"
    "movl (%%eax), %%eax\n\t"
    "movl %%eax, %0\n\t"
    : "=r" (Value)
    :
    : "eax", "memory"
    );
#elif defined (MDE_CPU_AARCH64)
  __asm__ __volatile__ (
    "mov x9, xzr\n\t"
    "ldr %0, [x9]\n\t"
    : "=r" (Value)
    :
    : "x9", "memory"
    );
#elif defined (MDE_CPU_RISCV64)
  __asm__ __volatile__ (
    "ld %0, 0(x0)\n\t"
    : "=r" (Value)
    :
    : "memory"
    );
#elif defined (MDE_CPU_LOONGARCH64)
  __asm__ __volatile__ (
    "ld.d %0, $r0, 0\n\t"
    : "=r" (Value)
    :
    : "memory"
    );
#else
  Value = 0;
  #error "NullAddressProbe needs an architecture-specific zero-address load"
#endif

  return Value;
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINTN  Value;

#if defined (MDE_CPU_LOONGARCH64)
  DumpVa0PageTable ();
#endif

  Print (L"NullAddressProbe: about to load from virtual address 0x0.\r\n");
  Print (L"NullAddressProbe: if NULL protection works, a CPU exception should occur now.\r\n");

  Value = ReadNullAddress ();

  Print (L"NullAddressProbe: load from 0x0 succeeded: 0x%lx\r\n", (UINT64)Value);
#if defined (MDE_CPU_LOONGARCH64)
  DumpVa0TlbEntry ();
#endif
  Print (L"NullAddressProbe: DONE; address 0x0 is readable in this firmware mapping.\r\n");

  return EFI_SUCCESS;
}
