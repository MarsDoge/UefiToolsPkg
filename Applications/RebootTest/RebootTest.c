/** @file
  Controlled reboot-cycle test for the UEFI Shell.

  The application stores test state in a file on the same filesystem as the
  executable. A startup.nsh script can invoke the continue command after each
  successful return to the UEFI Shell.

  Copyright (c) 2026, MarsDoge. All rights reserved.<BR>
  SPDX-License-Identifier: Apache-2.0
**/

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/ShellCEntryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>

#define REBOOT_TEST_STATE_PATH       L"\\RebootTest.state"
#define REBOOT_TEST_STATE_SIGNATURE  SIGNATURE_32 ('R', 'B', 'T', '1')
#define REBOOT_TEST_STATE_VERSION    1
#define REBOOT_TEST_DEFAULT_DELAY    3
#define REBOOT_TEST_MAX_DELAY        60
#define REBOOT_TEST_MAX_COUNT        1000000

#pragma pack (1)
typedef struct {
  UINT32    Signature;
  UINT16    Version;
  BOOLEAN   Active;
  UINT8     ResetType;
  UINT32    TargetResets;
  UINT32    CompletedResets;
  UINT32    DelaySeconds;
  UINT32    Crc32;
} REBOOT_TEST_STATE;
#pragma pack ()

STATIC
EFI_STATUS
DeleteState (
  VOID
  );

STATIC
VOID
PrintUsage (
  VOID
  )
{
  Print (L"RebootTest - controlled UEFI Shell reboot-cycle test\r\n");
  Print (L"\r\n");
  Print (L"Usage:\r\n");
  Print (L"  RebootTest.efi start <count> [warm|cold] [delay-seconds]\r\n");
  Print (L"  RebootTest.efi continue\r\n");
  Print (L"  RebootTest.efi status\r\n");
  Print (L"  RebootTest.efi stop\r\n");
  Print (L"  RebootTest.efi clear\r\n");
  Print (L"\r\n");
  Print (L"Deploy startup.nsh and a same-directory copy of this app named startup.nsh.efi.\r\n");
  Print (L"Press any key during the reset delay to stop an active test.\r\n");
}

STATIC
BOOLEAN
StrEqualsIgnoreCase (
  IN CONST CHAR16  *First,
  IN CONST CHAR16  *Second
  )
{
  CHAR16  FirstChar;
  CHAR16  SecondChar;

  if ((First == NULL) || (Second == NULL)) {
    return FALSE;
  }

  while ((*First != L'\0') && (*Second != L'\0')) {
    FirstChar  = *First;
    SecondChar = *Second;
    if ((FirstChar >= L'A') && (FirstChar <= L'Z')) {
      FirstChar = (CHAR16)(FirstChar - L'A' + L'a');
    }

    if ((SecondChar >= L'A') && (SecondChar <= L'Z')) {
      SecondChar = (CHAR16)(SecondChar - L'A' + L'a');
    }

    if (FirstChar != SecondChar) {
      return FALSE;
    }

    First++;
    Second++;
  }

  return (*First == *Second);
}

STATIC
BOOLEAN
ParseUint32 (
  IN  CONST CHAR16  *String,
  OUT UINT32        *Result
  )
{
  UINT32  Digit;
  UINT32  Value;

  if ((String == NULL) || (Result == NULL) || (*String == L'\0')) {
    return FALSE;
  }

  Value = 0;
  while (*String != L'\0') {
    if ((*String < L'0') || (*String > L'9')) {
      return FALSE;
    }

    Digit = (UINT32)(*String - L'0');
    if (Value > ((MAX_UINT32 - Digit) / 10)) {
      return FALSE;
    }

    Value = (Value * 10) + Digit;
    String++;
  }

  *Result = Value;
  return TRUE;
}

STATIC
EFI_STATUS
OpenStateRoot (
  OUT EFI_FILE_PROTOCOL  **Root
  )
{
  EFI_STATUS                       Status;
  EFI_LOADED_IMAGE_PROTOCOL        *LoadedImage;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *FileSystem;

  if (Root == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *Root = NULL;

  Status = gBS->HandleProtocol (
                  gImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **)&LoadedImage
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->HandleProtocol (
                  LoadedImage->DeviceHandle,
                  &gEfiSimpleFileSystemProtocolGuid,
                  (VOID **)&FileSystem
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return FileSystem->OpenVolume (FileSystem, Root);
}

STATIC
EFI_STATUS
CalculateStateCrc32 (
  IN OUT REBOOT_TEST_STATE  *State,
  OUT    UINT32             *Crc32
  )
{
  EFI_STATUS  Status;
  UINT32      SavedCrc32;

  if ((State == NULL) || (Crc32 == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  SavedCrc32   = State->Crc32;
  State->Crc32 = 0;
  Status       = gBS->CalculateCrc32 (State, sizeof (*State), Crc32);
  State->Crc32 = SavedCrc32;

  return Status;
}

STATIC
EFI_STATUS
LoadState (
  OUT REBOOT_TEST_STATE  *State
  )
{
  EFI_FILE_PROTOCOL  *File;
  EFI_FILE_PROTOCOL  *Root;
  EFI_STATUS         Status;
  UINT32             Crc32;
  UINT8              ExtraData;
  UINTN              ExtraSize;
  UINTN              Size;

  if (State == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Root   = NULL;
  File   = NULL;
  Status = OpenStateRoot (&Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Root->Open (
                   Root,
                   &File,
                   REBOOT_TEST_STATE_PATH,
                   EFI_FILE_MODE_READ,
                   0
                   );
  if (EFI_ERROR (Status)) {
    Root->Close (Root);
    return Status;
  }

  Size   = sizeof (*State);
  Status = File->Read (File, &Size, State);
  if (!EFI_ERROR (Status) && (Size == sizeof (*State))) {
    ExtraSize = sizeof (ExtraData);
    Status    = File->Read (File, &ExtraSize, &ExtraData);
    if (!EFI_ERROR (Status) && (ExtraSize != 0)) {
      Status = EFI_COMPROMISED_DATA;
    }
  }

  File->Close (File);
  Root->Close (Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((Size != sizeof (*State)) ||
      (State->Signature != REBOOT_TEST_STATE_SIGNATURE) ||
      (State->Version != REBOOT_TEST_STATE_VERSION))
  {
    return EFI_COMPROMISED_DATA;
  }

  Status = CalculateStateCrc32 (State, &Crc32);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (Crc32 != State->Crc32) {
    return EFI_CRC_ERROR;
  }

  if ((State->TargetResets == 0) ||
      (State->TargetResets > REBOOT_TEST_MAX_COUNT) ||
      (State->CompletedResets > State->TargetResets) ||
      (State->Active && (State->CompletedResets >= State->TargetResets)) ||
      (State->DelaySeconds == 0) ||
      (State->DelaySeconds > REBOOT_TEST_MAX_DELAY) ||
      ((State->Active != FALSE) && (State->Active != TRUE)) ||
      ((State->ResetType != EfiResetWarm) &&
       (State->ResetType != EfiResetCold)))
  {
    return EFI_COMPROMISED_DATA;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
SaveState (
  IN CONST REBOOT_TEST_STATE  *State
  )
{
  EFI_FILE_PROTOCOL   *File;
  EFI_FILE_PROTOCOL   *Root;
  EFI_STATUS          CloseStatus;
  EFI_STATUS          Status;
  REBOOT_TEST_STATE   LocalState;
  UINTN               Size;

  if (State == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  CopyMem (&LocalState, State, sizeof (LocalState));
  LocalState.Crc32 = 0;
  Status           = gBS->CalculateCrc32 (
                            &LocalState,
                            sizeof (LocalState),
                            &LocalState.Crc32
                            );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // EFI_FILE_MODE_CREATE does not truncate an existing file. Delete the old
  // state first so a successfully saved state always has the exact expected
  // length. Losing the file before recreation fails closed on the next boot.
  //
  Status = DeleteState ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Root   = NULL;
  File   = NULL;
  Status = OpenStateRoot (&Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Root->Open (
                   Root,
                   &File,
                   REBOOT_TEST_STATE_PATH,
                   EFI_FILE_MODE_READ |
                   EFI_FILE_MODE_WRITE |
                   EFI_FILE_MODE_CREATE,
                   0
                   );
  if (EFI_ERROR (Status)) {
    Root->Close (Root);
    return Status;
  }

  Status = File->SetPosition (File, 0);
  if (!EFI_ERROR (Status)) {
    Size   = sizeof (LocalState);
    Status = File->Write (File, &Size, &LocalState);
    if (!EFI_ERROR (Status) && (Size != sizeof (LocalState))) {
      Status = EFI_DEVICE_ERROR;
    }
  }

  if (!EFI_ERROR (Status)) {
    Status = File->Flush (File);
  }

  CloseStatus = File->Close (File);
  if (!EFI_ERROR (Status) && EFI_ERROR (CloseStatus)) {
    Status = CloseStatus;
  }

  CloseStatus = Root->Close (Root);
  if (!EFI_ERROR (Status) && EFI_ERROR (CloseStatus)) {
    Status = CloseStatus;
  }

  return Status;
}

STATIC
EFI_STATUS
DeleteState (
  VOID
  )
{
  EFI_FILE_PROTOCOL  *File;
  EFI_FILE_PROTOCOL  *Root;
  EFI_STATUS         CloseStatus;
  EFI_STATUS         Status;

  Root   = NULL;
  File   = NULL;
  Status = OpenStateRoot (&Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Root->Open (
                   Root,
                   &File,
                   REBOOT_TEST_STATE_PATH,
                   EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
                   0
                   );
  if (Status == EFI_NOT_FOUND) {
    CloseStatus = Root->Close (Root);
    return EFI_ERROR (CloseStatus) ? CloseStatus : EFI_SUCCESS;
  }

  if (EFI_ERROR (Status)) {
    Root->Close (Root);
    return Status;
  }

  Status = File->Delete (File);
  if (Status != EFI_SUCCESS) {
    Root->Close (Root);
    return EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
  }

  //
  // Delete() closes File. Verify that the name is no longer reachable before
  // reporting success; an active state must never survive a successful clear.
  //
  File   = NULL;
  Status = Root->Open (
                   Root,
                   &File,
                   REBOOT_TEST_STATE_PATH,
                   EFI_FILE_MODE_READ,
                   0
                   );
  if (Status == EFI_NOT_FOUND) {
    Status = EFI_SUCCESS;
  } else if (Status == EFI_SUCCESS) {
    File->Close (File);
    Status = EFI_DEVICE_ERROR;
  } else if (!EFI_ERROR (Status)) {
    Status = EFI_DEVICE_ERROR;
  }

  CloseStatus = Root->Close (Root);
  if (!EFI_ERROR (Status) && EFI_ERROR (CloseStatus)) {
    Status = CloseStatus;
  }

  return Status;
}

STATIC
EFI_STATUS
WaitForResetOrKey (
  IN UINT32  DelaySeconds
  )
{
  EFI_EVENT     Events[2];
  EFI_EVENT     TimerEvent;
  EFI_INPUT_KEY Key;
  EFI_STATUS    Status;
  UINTN         EventIndex;

  Print (
    L"Reset in %u second(s); press any key to stop the test...\r\n",
    DelaySeconds
    );

  Status = gBS->CreateEvent (
                  EVT_TIMER,
                  TPL_CALLBACK,
                  NULL,
                  NULL,
                  &TimerEvent
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->SetTimer (
                  TimerEvent,
                  TimerRelative,
                  MultU64x32 (10000000, DelaySeconds)
                  );
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (TimerEvent);
    return Status;
  }

  Events[0] = gST->ConIn->WaitForKey;
  Events[1] = TimerEvent;
  Status    = gBS->WaitForEvent (ARRAY_SIZE (Events), Events, &EventIndex);
  gBS->CloseEvent (TimerEvent);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (EventIndex == 0) {
    gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
    return EFI_ABORTED;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ResetSystemForTest (
  IN OUT REBOOT_TEST_STATE  *State
  )
{
  EFI_STATUS  Status;

  Status = WaitForResetOrKey (State->DelaySeconds);
  if (Status == EFI_ABORTED) {
    State->Active = FALSE;
    Status        = SaveState (State);
    if (EFI_ERROR (Status)) {
      Print (L"RebootTest: failed to save stopped state: %r\r\n", Status);
      return Status;
    }

    Print (L"RebootTest: stopped by user.\r\n");
    return EFI_ABORTED;
  }

  if (EFI_ERROR (Status)) {
    return Status;
  }

  Print (
    L"RebootTest: requesting a %s reset.\r\n",
    (State->ResetType == EfiResetWarm) ? L"warm" : L"cold"
    );

  gRT->ResetSystem ((EFI_RESET_TYPE)State->ResetType, EFI_SUCCESS, 0, NULL);

  State->Active = FALSE;
  Status        = SaveState (State);
  if (EFI_ERROR (Status)) {
    Print (L"RebootTest: ResetSystem returned and state cleanup failed: %r\r\n", Status);
  } else {
    Print (L"RebootTest: ResetSystem returned unexpectedly; test disabled.\r\n");
  }

  return EFI_DEVICE_ERROR;
}

STATIC
EFI_STATUS
StartTest (
  IN UINTN   Argc,
  IN CHAR16  **Argv
  )
{
  EFI_STATUS         Status;
  REBOOT_TEST_STATE  State;
  UINT32             Count;
  UINT32             DelaySeconds;

  if ((Argc < 3) || (Argc > 5) ||
      !ParseUint32 (Argv[2], &Count) ||
      (Count == 0) || (Count > REBOOT_TEST_MAX_COUNT))
  {
    Print (
      L"RebootTest: count must be between 1 and %u.\r\n",
      REBOOT_TEST_MAX_COUNT
      );
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (&State, sizeof (State));
  State.Signature    = REBOOT_TEST_STATE_SIGNATURE;
  State.Version      = REBOOT_TEST_STATE_VERSION;
  State.Active       = TRUE;
  State.ResetType    = EfiResetWarm;
  State.TargetResets = Count;
  State.DelaySeconds = REBOOT_TEST_DEFAULT_DELAY;

  if (Argc >= 4) {
    if (StrEqualsIgnoreCase (Argv[3], L"warm")) {
      State.ResetType = EfiResetWarm;
    } else if (StrEqualsIgnoreCase (Argv[3], L"cold")) {
      State.ResetType = EfiResetCold;
    } else {
      Print (L"RebootTest: reset type must be warm or cold.\r\n");
      return EFI_INVALID_PARAMETER;
    }
  }

  if (Argc == 5) {
    if (!ParseUint32 (Argv[4], &DelaySeconds) ||
        (DelaySeconds == 0) ||
        (DelaySeconds > REBOOT_TEST_MAX_DELAY))
    {
      Print (
        L"RebootTest: delay must be between 1 and %u seconds.\r\n",
        REBOOT_TEST_MAX_DELAY
        );
      return EFI_INVALID_PARAMETER;
    }

    State.DelaySeconds = DelaySeconds;
  }

  Status = SaveState (&State);
  if (EFI_ERROR (Status)) {
    Print (L"RebootTest: failed to create state file: %r\r\n", Status);
    return Status;
  }

  Print (
    L"RebootTest: armed for %u %s reset(s); completed=0.\r\n",
    State.TargetResets,
    (State.ResetType == EfiResetWarm) ? L"warm" : L"cold"
    );
  return ResetSystemForTest (&State);
}

STATIC
EFI_STATUS
ContinueTest (
  VOID
  )
{
  EFI_STATUS         Status;
  REBOOT_TEST_STATE  State;

  Status = LoadState (&State);
  if (Status == EFI_NOT_FOUND) {
    Print (L"RebootTest: no test state; startup hook is idle.\r\n");
    return EFI_SUCCESS;
  }

  if (EFI_ERROR (Status)) {
    Print (L"RebootTest: invalid state file: %r; refusing to reset.\r\n", Status);
    return Status;
  }

  if (!State.Active) {
    Print (
      L"RebootTest: test is inactive; completed=%u/%u.\r\n",
      State.CompletedResets,
      State.TargetResets
      );
    return EFI_SUCCESS;
  }

  State.CompletedResets++;
  if (State.CompletedResets >= State.TargetResets) {
    State.Active = FALSE;
    Status       = SaveState (&State);
    if (EFI_ERROR (Status)) {
      Print (L"RebootTest: test completed but state save failed: %r\r\n", Status);
      return Status;
    }

    Print (
      L"RebootTest: PASS; completed %u/%u reset cycle(s).\r\n",
      State.CompletedResets,
      State.TargetResets
      );
    return EFI_SUCCESS;
  }

  Status = SaveState (&State);
  if (EFI_ERROR (Status)) {
    Print (L"RebootTest: failed to save progress: %r; refusing to reset.\r\n", Status);
    return Status;
  }

  Print (
    L"RebootTest: completed %u/%u reset cycle(s).\r\n",
    State.CompletedResets,
    State.TargetResets
    );
  return ResetSystemForTest (&State);
}

STATIC
EFI_STATUS
ShowStatus (
  VOID
  )
{
  EFI_STATUS         Status;
  REBOOT_TEST_STATE  State;

  Status = LoadState (&State);
  if (Status == EFI_NOT_FOUND) {
    Print (L"RebootTest: no state file.\r\n");
    return EFI_SUCCESS;
  }

  if (EFI_ERROR (Status)) {
    Print (L"RebootTest: invalid state file: %r.\r\n", Status);
    return Status;
  }

  Print (L"RebootTest status:\r\n");
  Print (L"  Active:    %s\r\n", State.Active ? L"yes" : L"no");
  Print (L"  Reset:     %s\r\n", (State.ResetType == EfiResetWarm) ? L"warm" : L"cold");
  Print (L"  Completed: %u/%u\r\n", State.CompletedResets, State.TargetResets);
  Print (L"  Delay:     %u second(s)\r\n", State.DelaySeconds);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
StopTest (
  VOID
  )
{
  EFI_STATUS         Status;
  REBOOT_TEST_STATE  State;

  Status = LoadState (&State);
  if (Status == EFI_NOT_FOUND) {
    Print (L"RebootTest: no state file.\r\n");
    return EFI_SUCCESS;
  }

  if (EFI_ERROR (Status)) {
    Print (L"RebootTest: invalid state file: %r. Use clear to remove it.\r\n", Status);
    return Status;
  }

  State.Active = FALSE;
  Status       = SaveState (&State);
  if (EFI_ERROR (Status)) {
    Print (L"RebootTest: failed to save stopped state: %r\r\n", Status);
    return Status;
  }

  Print (
    L"RebootTest: stopped at %u/%u completed reset cycle(s).\r\n",
    State.CompletedResets,
    State.TargetResets
    );
  return EFI_SUCCESS;
}

INTN
EFIAPI
ShellAppMain (
  IN UINTN   Argc,
  IN CHAR16  **Argv
  )
{
  EFI_STATUS  Status;

  if (Argc < 2) {
    PrintUsage ();
    return (INTN)EFI_INVALID_PARAMETER;
  }

  if (StrEqualsIgnoreCase (Argv[1], L"start")) {
    Status = StartTest (Argc, Argv);
  } else if (StrEqualsIgnoreCase (Argv[1], L"continue")) {
    Status = ContinueTest ();
  } else if (StrEqualsIgnoreCase (Argv[1], L"status")) {
    Status = ShowStatus ();
  } else if (StrEqualsIgnoreCase (Argv[1], L"stop")) {
    Status = StopTest ();
  } else if (StrEqualsIgnoreCase (Argv[1], L"clear")) {
    Status = DeleteState ();
    if (EFI_ERROR (Status)) {
      Print (L"RebootTest: failed to delete state file: %r\r\n", Status);
    } else {
      Print (L"RebootTest: state file removed.\r\n");
    }
  } else {
    PrintUsage ();
    Status = EFI_INVALID_PARAMETER;
  }

  return (INTN)Status;
}
