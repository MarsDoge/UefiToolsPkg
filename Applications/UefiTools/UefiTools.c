/** @file
  Dispatch the UEFI tool suite from one Shell application.

  Copyright (c) 2026, MarsDoge. All rights reserved.<BR>
  SPDX-License-Identifier: Apache-2.0
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/ShellParameters.h>

typedef EFI_STATUS (EFIAPI *TOOL_MAIN)(EFI_HANDLE, EFI_SYSTEM_TABLE *);

#define DECLARE_TOOL(Name) \
  EFI_STATUS EFIAPI Name (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)

DECLARE_TOOL (FillNvVarsMain);
DECLARE_TOOL (LoongArchMappingDumpMain);
DECLARE_TOOL (NullAddressProbeMain);
DECLARE_TOOL (PciOptionRomInfoMain);
DECLARE_TOOL (PciTopologyMain);
DECLARE_TOOL (RuntimeVarEditorMain);

INTN
EFIAPI
RebootTestMain (
  IN UINTN   Argc,
  IN CHAR16  **Argv
  );

typedef struct {
  CONST CHAR16  *Name;
  CONST CHAR16  *Description;
  TOOL_MAIN     Main;
} TOOL_COMMAND;

typedef enum {
  CommandPciTopology,
  CommandPciOptionRomInfo,
  CommandLoongArchMapping,
  CommandRuntimeVarEditor,
  CommandRebootTest,
  CommandFillNvVars,
  CommandNullAddressProbe
} TOOL_COMMAND_INDEX;

STATIC CONST TOOL_COMMAND  mCommands[] = {
  { L"pci-topology",       L"Show firmware-visible PCI topology", PciTopologyMain },
  { L"pci-option-rom-info", L"Inspect PCI Option ROM images",       PciOptionRomInfoMain },
  { L"loongarch-mapping",  L"Dump LoongArch page tables and TLB", LoongArchMappingDumpMain },
  { L"runtime-var-editor", L"Open the runtime variable editor",   RuntimeVarEditorMain },
  { L"reboot-test",        L"Run a finite reboot-cycle test",     NULL },
  { L"fill-nv-vars",       L"Fill the NV variable store and reset", FillNvVarsMain },
  { L"null-address-probe", L"Probe address zero (may fault)",     NullAddressProbeMain }
};

STATIC
VOID
PrintUsage (
  VOID
  )
{
  UINTN  Index;

  Print (L"Usage: UefiTools.efi <command> [args]\r\nCommands:\r\n");
  for (Index = 0; Index < ARRAY_SIZE (mCommands); Index++) {
    Print (L"  %-21s %s\r\n", mCommands[Index].Name, mCommands[Index].Description);
  }

  Print (L"  help                  Show this list\r\n");
  Print (L"Example: UefiTools.efi reboot-test start 20 warm 3\r\n");
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                     Status;
  EFI_SHELL_PARAMETERS_PROTOCOL  *Parameters;
  UINTN                          Index;
  UINTN                          SavedArgc;
  CHAR16                         **SavedArgv;

  Status = gBS->HandleProtocol (
                  ImageHandle,
                  &gEfiShellParametersProtocolGuid,
                  (VOID **)&Parameters
                  );
  if (EFI_ERROR (Status)) {
    Print (L"UefiTools requires the UEFI Shell parameters protocol: %r\r\n", Status);
    return Status;
  }

  if ((Parameters->Argc < 2) ||
      (StrCmp (Parameters->Argv[1], L"help") == 0) ||
      (StrCmp (Parameters->Argv[1], L"--help") == 0)) {
    PrintUsage ();
    return EFI_SUCCESS;
  }

  for (Index = 0; Index < ARRAY_SIZE (mCommands); Index++) {
    if (StrCmp (Parameters->Argv[1], mCommands[Index].Name) != 0) {
      continue;
    }

    if (Index == CommandRebootTest) {
      // RebootTest expects argv[0] to name the executable and argv[1] to
      // contain its own action (start/continue/status/stop/clear).
      return (EFI_STATUS)RebootTestMain (Parameters->Argc - 1, Parameters->Argv + 1);
    }

    if (Index == CommandLoongArchMapping) {
      // The original mapping tool reads Shell parameters on ImageHandle.
      // Shift only for this synchronous call, then restore the protocol.
      SavedArgc        = Parameters->Argc;
      SavedArgv        = Parameters->Argv;
      Parameters->Argc = SavedArgc - 1;
      Parameters->Argv = SavedArgv + 1;
      Status           = mCommands[Index].Main (ImageHandle, SystemTable);
      Parameters->Argc = SavedArgc;
      Parameters->Argv = SavedArgv;
      return Status;
    }

    return mCommands[Index].Main (ImageHandle, SystemTable);
  }

  Print (L"Unknown command: %s\r\n", Parameters->Argv[1]);
  PrintUsage ();
  return EFI_INVALID_PARAMETER;
}
