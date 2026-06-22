/** @file
  Fill UEFI non-volatile variable storage for boot option diagnostics.

  This helper is intended for QEMU/OVMF manual reproduction of failures where
  BDS can enumerate a bootable device, but cannot persist the auto-created
  Boot#### variable because the non-volatile variable store is full.

  Copyright (c) 2026, MarsDoge
  SPDX-License-Identifier: Apache-2.0
**/

#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#define FILL_NV_VAR_DATA_SIZE  1024
#define FILL_NV_VAR_MAX_COUNT  4096

STATIC CONST UINTN  mFillNvVarDataSizes[] = {
  1024,
  512,
  256,
  128,
  64,
  32
};

STATIC EFI_GUID  mFillNvVarsGuid = {
  0x6f8d32b2, 0x7dfd, 0x4f8e, { 0x9a, 0x76, 0x91, 0x4d, 0x61, 0xe1, 0x1d, 0x2e }
};

/**
  Application entry point.

  @param[in] ImageHandle  The firmware allocated handle for the EFI image.
  @param[in] SystemTable  A pointer to the EFI System Table.

  @retval EFI_SUCCESS     Variables were written until the variable service
                          reported an error, then the system was reset.
  @retval Others          Failed before filling could start.
**/
EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  UINT8       *Data;
  CHAR16      Name[32];
  UINTN       Index;
  UINTN       SizeIndex;
  UINTN       DataSize;

  Data = AllocatePool (FILL_NV_VAR_DATA_SIZE);
  if (Data == NULL) {
    Print (L"FillNvVars: failed to allocate payload\r\n");
    return EFI_OUT_OF_RESOURCES;
  }

  SetMem (Data, FILL_NV_VAR_DATA_SIZE, 0x5A);

  Print (L"FillNvVars: filling NV variable store...\r\n");

  for (SizeIndex = 0; SizeIndex < ARRAY_SIZE (mFillNvVarDataSizes); SizeIndex++) {
    DataSize = mFillNvVarDataSizes[SizeIndex];

    for (Index = 0; Index < FILL_NV_VAR_MAX_COUNT; Index++) {
      UnicodeSPrint (Name, sizeof (Name), L"FillNv%04x%04x", (UINT32)DataSize, (UINT32)Index);

      Status = gRT->SetVariable (
                      Name,
                      &mFillNvVarsGuid,
                      EFI_VARIABLE_NON_VOLATILE |
                      EFI_VARIABLE_BOOTSERVICE_ACCESS |
                      EFI_VARIABLE_RUNTIME_ACCESS,
                      DataSize,
                      Data
                      );
      if (EFI_ERROR (Status)) {
        Print (
          L"FillNvVars: SetVariable(%s, %u bytes) stopped at index %u: %r\r\n",
          Name,
          (UINT32)DataSize,
          (UINT32)Index,
          Status
          );
        DEBUG ((
          DEBUG_ERROR,
          "FillNvVars: SetVariable(%s, %u bytes) stopped at index %u: %r\n",
          Name,
          (UINT32)DataSize,
          (UINT32)Index,
          Status
          ));
        break;
      }
    }
  }

  FreePool (Data);

  Print (L"FillNvVars: resetting system for next test phase...\r\n");
  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);

  return EFI_SUCCESS;
}
