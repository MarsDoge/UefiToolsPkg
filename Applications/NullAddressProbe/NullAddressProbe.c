/** @file
  Probe whether virtual address 0 is readable in the current UEFI mapping.

  This diagnostic intentionally performs a real architecture load from address
  zero. If null-pointer detection is implemented by the firmware page tables,
  the load should raise a CPU exception before the success line is printed.

  Copyright (c) 2026, MarsDoge. All rights reserved.<BR>
  SPDX-License-Identifier: Apache-2.0
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/UefiLib.h>

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

  Print (L"NullAddressProbe: about to load from virtual address 0x0.\r\n");
  Print (L"NullAddressProbe: if NULL protection works, a CPU exception should occur now.\r\n");

  Value = ReadNullAddress ();

  Print (L"NullAddressProbe: load from 0x0 succeeded: 0x%lx\r\n", (UINT64)Value);
  Print (L"NullAddressProbe: DONE; address 0x0 is readable in this firmware mapping.\r\n");

  return EFI_SUCCESS;
}
