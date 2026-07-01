/** @file
  Graphical runtime UEFI variable browser and editor.

  Enumerates variables that have EFI_VARIABLE_RUNTIME_ACCESS, displays their
  name, vendor GUID, attributes, size and status, and allows lab users to edit
  one byte, update attributes, or delete a variable.

  Copyright (c) 2026, MarsDoge
  SPDX-License-Identifier: Apache-2.0
**/

#include <Uefi.h>

#include <Protocol/GraphicsOutput.h>
#include <Protocol/HiiFont.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#define RVE_MAX_VARIABLES       2048
#define RVE_INITIAL_NAME_BYTES  256
#define RVE_MAX_NAME_CHARS      512
#define RVE_MAX_EDIT_DATA       (64 * 1024)
#define RVE_LIST_ROW_HEIGHT     22
#define RVE_LEFT_MARGIN         18
#define RVE_TOP_MARGIN          14
#define RVE_TEXT_HEIGHT         18
#define RVE_HEX_PAGE_BYTES      256

typedef struct {
  CHAR16      *Name;
  EFI_GUID    VendorGuid;
  UINT32      Attributes;
  UINTN       DataSize;
  EFI_STATUS  Status;
} RVE_VARIABLE;

typedef struct {
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  EFI_HII_FONT_PROTOCOL         *Font;
  UINTN                         Width;
  UINTN                         Height;
  BOOLEAN                       Graphics;
} RVE_CONTEXT;

STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mColorBg       = { 0x18, 0x12, 0x0D, 0x00 };
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mColorPanel    = { 0x2B, 0x22, 0x17, 0x00 };
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mColorHeader   = { 0x77, 0x47, 0x16, 0x00 };
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mColorSelect   = { 0x5C, 0x39, 0x18, 0x00 };
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mColorText     = { 0xE8, 0xE8, 0xDC, 0x00 };
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mColorMuted    = { 0xA8, 0xA8, 0x9A, 0x00 };
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mColorWarn     = { 0x30, 0xC8, 0xFF, 0x00 };
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mColorDanger   = { 0x58, 0x58, 0xF0, 0x00 };
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mColorOk       = { 0x84, 0xD6, 0x7A, 0x00 };

STATIC
VOID
RveFreeVariables (
  IN OUT RVE_VARIABLE  *Variables,
  IN     UINTN         Count
  )
{
  UINTN  Index;

  if (Variables == NULL) {
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    if (Variables[Index].Name != NULL) {
      FreePool (Variables[Index].Name);
    }
  }

  FreePool (Variables);
}

STATIC
EFI_STATUS
RveFillRect (
  IN RVE_CONTEXT                    *Context,
  IN UINTN                          X,
  IN UINTN                          Y,
  IN UINTN                          Width,
  IN UINTN                          Height,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color
  )
{
  if ((Context == NULL) || (Context->Gop == NULL) || (Width == 0) || (Height == 0)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((X >= Context->Width) || (Y >= Context->Height)) {
    return EFI_SUCCESS;
  }

  if (Width > (Context->Width - X)) {
    Width = Context->Width - X;
  }

  if (Height > (Context->Height - Y)) {
    Height = Context->Height - Y;
  }

  return Context->Gop->Blt (
                         Context->Gop,
                         &Color,
                         EfiBltVideoFill,
                         0,
                         0,
                         X,
                         Y,
                         Width,
                         Height,
                         0
                         );
}

STATIC
EFI_STATUS
RveDrawText (
  IN RVE_CONTEXT                    *Context,
  IN UINTN                          X,
  IN UINTN                          Y,
  IN CONST CHAR16                   *Text,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Foreground,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Background
  )
{
  EFI_IMAGE_OUTPUT       *Blt;
  EFI_FONT_DISPLAY_INFO  FontInfo;
  EFI_STATUS             Status;

  if ((Context == NULL) || (Text == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (!Context->Graphics || (Context->Font == NULL) || (Context->Gop == NULL)) {
    Print (L"%s\r\n", Text);
    return EFI_SUCCESS;
  }

  Blt = AllocateZeroPool (sizeof (*Blt));
  if (Blt == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  ZeroMem (&FontInfo, sizeof (FontInfo));
  FontInfo.ForegroundColor = Foreground;
  FontInfo.BackgroundColor = Background;

  Blt->Width        = (UINT16)Context->Width;
  Blt->Height       = (UINT16)Context->Height;
  Blt->Image.Screen = Context->Gop;

  Status = Context->Font->StringToImage (
                            Context->Font,
                            EFI_HII_IGNORE_IF_NO_GLYPH | EFI_HII_OUT_FLAG_CLIP |
                            EFI_HII_OUT_FLAG_CLIP_CLEAN_X | EFI_HII_OUT_FLAG_CLIP_CLEAN_Y |
                            EFI_HII_IGNORE_LINE_BREAK | EFI_HII_DIRECT_TO_SCREEN,
                            (EFI_STRING)Text,
                            &FontInfo,
                            &Blt,
                            X,
                            Y,
                            NULL,
                            NULL,
                            NULL
                            );
  FreePool (Blt);
  return Status;
}

STATIC
VOID
RveGuidToString (
  IN  CONST EFI_GUID  *Guid,
  OUT CHAR16          *Buffer,
  IN  UINTN           BufferChars
  )
{
  if ((Guid == NULL) || (Buffer == NULL) || (BufferChars == 0)) {
    return;
  }

  UnicodeSPrint (
    Buffer,
    BufferChars * sizeof (CHAR16),
    L"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
    Guid->Data1,
    Guid->Data2,
    Guid->Data3,
    Guid->Data4[0],
    Guid->Data4[1],
    Guid->Data4[2],
    Guid->Data4[3],
    Guid->Data4[4],
    Guid->Data4[5],
    Guid->Data4[6],
    Guid->Data4[7]
    );
}

STATIC
VOID
RveAttrsToString (
  IN  UINT32  Attributes,
  OUT CHAR16  *Buffer,
  IN  UINTN   BufferChars
  )
{
  UnicodeSPrint (
    Buffer,
    BufferChars * sizeof (CHAR16),
    L"%s%s%s%s%s%s%s0x%08x",
    (Attributes & EFI_VARIABLE_NON_VOLATILE) != 0 ? L"NV " : L"",
    (Attributes & EFI_VARIABLE_BOOTSERVICE_ACCESS) != 0 ? L"BS " : L"",
    (Attributes & EFI_VARIABLE_RUNTIME_ACCESS) != 0 ? L"RT " : L"",
    (Attributes & EFI_VARIABLE_HARDWARE_ERROR_RECORD) != 0 ? L"HW " : L"",
    (Attributes & EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS) != 0 ? L"AUTH " : L"",
    (Attributes & EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS) != 0 ? L"TIMEAUTH " : L"",
    (Attributes & EFI_VARIABLE_APPEND_WRITE) != 0 ? L"APPEND " : L"",
    Attributes
    );
}

STATIC
BOOLEAN
RveIsAuthenticatedVariable (
  IN UINT32  Attributes
  )
{
  return (BOOLEAN)((Attributes & (EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS | EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS)) != 0);
}

STATIC
UINT32
RveNormalWriteAttributes (
  IN UINT32  Attributes
  )
{
  return Attributes & ~EFI_VARIABLE_APPEND_WRITE;
}

STATIC
INTN
RveCompareVariables (
  IN CONST VOID  *Left,
  IN CONST VOID  *Right
  )
{
  CONST RVE_VARIABLE  *L;
  CONST RVE_VARIABLE  *R;

  L = (CONST RVE_VARIABLE *)Left;
  R = (CONST RVE_VARIABLE *)Right;
  return StrCmp (L->Name, R->Name);
}

STATIC
VOID
RveSortVariables (
  IN OUT RVE_VARIABLE  *Variables,
  IN     UINTN         Count
  )
{
  UINTN         Index;
  UINTN         Insert;
  RVE_VARIABLE  Key;

  if ((Variables == NULL) || (Count < 2)) {
    return;
  }

  for (Index = 1; Index < Count; Index++) {
    CopyMem (&Key, &Variables[Index], sizeof (Key));
    Insert = Index;
    while ((Insert > 0) && (RveCompareVariables (&Variables[Insert - 1], &Key) > 0)) {
      CopyMem (&Variables[Insert], &Variables[Insert - 1], sizeof (Variables[Insert]));
      Insert--;
    }

    CopyMem (&Variables[Insert], &Key, sizeof (Variables[Insert]));
  }
}

STATIC
EFI_STATUS
RveReadVariableInfo (
  IN OUT RVE_VARIABLE  *Variable
  )
{
  EFI_STATUS  Status;
  UINTN       DataSize;
  UINT32      Attributes;

  if ((Variable == NULL) || (Variable->Name == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  DataSize   = 0;
  Attributes = 0;
  Status = gRT->GetVariable (
                  Variable->Name,
                  &Variable->VendorGuid,
                  &Attributes,
                  &DataSize,
                  NULL
                  );

  if ((Status == EFI_BUFFER_TOO_SMALL) || (Status == EFI_SUCCESS)) {
    Variable->Attributes = Attributes;
    Variable->DataSize   = DataSize;
    Variable->Status     = EFI_SUCCESS;
    return EFI_SUCCESS;
  }

  Variable->Attributes = Attributes;
  Variable->DataSize   = DataSize;
  Variable->Status     = Status;
  return Status;
}

STATIC
EFI_STATUS
RveLoadVariables (
  OUT RVE_VARIABLE  **VariablesOut,
  OUT UINTN         *CountOut,
  OUT CHAR16        *Message,
  IN  UINTN         MessageChars
  )
{
  EFI_STATUS    Status;
  RVE_VARIABLE  *Variables;
  UINTN         Count;
  UINTN         Capacity;
  UINTN         NameSize;
  UINTN         NameCapacity;
  CHAR16        *Name;
  EFI_GUID      Guid;
  UINTN         NameChars;
  CHAR16        *NameCopy;

  if ((VariablesOut == NULL) || (CountOut == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *VariablesOut = NULL;
  *CountOut     = 0;
  Capacity      = 128;
  Count         = 0;

  Variables = AllocateZeroPool (Capacity * sizeof (*Variables));
  if (Variables == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  NameCapacity = RVE_INITIAL_NAME_BYTES;
  Name = AllocateZeroPool (NameCapacity);
  if (Name == NULL) {
    FreePool (Variables);
    return EFI_OUT_OF_RESOURCES;
  }

  ZeroMem (&Guid, sizeof (Guid));
  Name[0] = L'\0';

  while (TRUE) {
    NameSize = NameCapacity;
    Status = gRT->GetNextVariableName (&NameSize, Name, &Guid);
    if (Status == EFI_BUFFER_TOO_SMALL) {
      CHAR16  *NewName;

      if ((NameSize > ((RVE_MAX_NAME_CHARS + 1) * sizeof (CHAR16))) || ((NameSize & (sizeof (CHAR16) - 1)) != 0)) {
        if (Message != NULL) {
          UnicodeSPrint (Message, MessageChars * sizeof (CHAR16), L"Stopped: variable name buffer request is invalid or too large.");
        }
        Status = EFI_BAD_BUFFER_SIZE;
        break;
      }

      NewName = ReallocatePool (NameCapacity, NameSize, Name);
      if (NewName == NULL) {
        Status = EFI_OUT_OF_RESOURCES;
        break;
      }

      Name         = NewName;
      NameCapacity = NameSize;
      Status       = gRT->GetNextVariableName (&NameSize, Name, &Guid);
    }

    if (Status == EFI_NOT_FOUND) {
      Status = EFI_SUCCESS;
      break;
    }

    if (EFI_ERROR (Status)) {
      break;
    }

    if (Count >= RVE_MAX_VARIABLES) {
      if (Message != NULL) {
        UnicodeSPrint (Message, MessageChars * sizeof (CHAR16), L"Stopped at %u variables (tool limit).", (UINT32)Count);
      }
      break;
    }

    NameChars = StrLen (Name);
    if (NameChars > RVE_MAX_NAME_CHARS) {
      continue;
    }

    NameCopy = AllocateCopyPool ((NameChars + 1) * sizeof (CHAR16), Name);
    if (NameCopy == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      break;
    }

    if (Count == Capacity) {
      RVE_VARIABLE  *NewVariables;

      NewVariables = ReallocatePool (Capacity * sizeof (*Variables), (Capacity * 2) * sizeof (*Variables), Variables);
      if (NewVariables == NULL) {
        FreePool (NameCopy);
        Status = EFI_OUT_OF_RESOURCES;
        break;
      }

      Variables = NewVariables;
      ZeroMem (&Variables[Capacity], Capacity * sizeof (*Variables));
      Capacity *= 2;
    }

    Variables[Count].Name       = NameCopy;
    Variables[Count].VendorGuid = Guid;
    Status = RveReadVariableInfo (&Variables[Count]);
    if (!EFI_ERROR (Status) && ((Variables[Count].Attributes & EFI_VARIABLE_RUNTIME_ACCESS) != 0)) {
      Count++;
    } else {
      FreePool (Variables[Count].Name);
      ZeroMem (&Variables[Count], sizeof (Variables[Count]));
    }
  }

  FreePool (Name);

  if (EFI_ERROR (Status)) {
    RveFreeVariables (Variables, Count);
    return Status;
  }

  if (Count > 1) {
    RveSortVariables (Variables, Count);
  }

  *VariablesOut = Variables;
  *CountOut     = Count;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
RveLoadVariableData (
  IN  RVE_VARIABLE  *Variable,
  OUT UINT8         **DataOut,
  OUT UINTN         *DataSizeOut,
  OUT UINT32        *AttributesOut
  )
{
  EFI_STATUS  Status;
  UINTN       DataSize;
  UINT32      Attributes;
  UINT8       *Data;

  if ((Variable == NULL) || (DataOut == NULL) || (DataSizeOut == NULL) || (AttributesOut == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *DataOut       = NULL;
  *DataSizeOut   = 0;
  *AttributesOut = 0;
  DataSize       = 0;
  Attributes     = 0;

  Status = gRT->GetVariable (Variable->Name, &Variable->VendorGuid, &Attributes, &DataSize, NULL);
  if ((Status != EFI_BUFFER_TOO_SMALL) && !((Status == EFI_SUCCESS) && (DataSize == 0))) {
    return Status;
  }

  if (DataSize > RVE_MAX_EDIT_DATA) {
    return EFI_VOLUME_FULL;
  }

  Data = AllocateZeroPool (DataSize == 0 ? 1 : DataSize);
  if (Data == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  if (DataSize != 0) {
    Status = gRT->GetVariable (Variable->Name, &Variable->VendorGuid, &Attributes, &DataSize, Data);
    if (EFI_ERROR (Status)) {
      FreePool (Data);
      return Status;
    }
  }

  *DataOut       = Data;
  *DataSizeOut   = DataSize;
  *AttributesOut = Attributes;
  return EFI_SUCCESS;
}

STATIC
VOID
RveInitContext (
  OUT RVE_CONTEXT  *Context
  )
{
  EFI_STATUS  Status;

  ZeroMem (Context, sizeof (*Context));
  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Context->Gop);
  if (!EFI_ERROR (Status) && (Context->Gop != NULL) && (Context->Gop->Mode != NULL) && (Context->Gop->Mode->Info != NULL)) {
    Context->Width    = Context->Gop->Mode->Info->HorizontalResolution;
    Context->Height   = Context->Gop->Mode->Info->VerticalResolution;
    Context->Graphics = TRUE;
  }

  Status = gBS->LocateProtocol (&gEfiHiiFontProtocolGuid, NULL, (VOID **)&Context->Font);
  if (EFI_ERROR (Status)) {
    Context->Font = NULL;
  }
}

STATIC
VOID
RveClear (
  IN RVE_CONTEXT  *Context
  )
{
  if ((Context != NULL) && Context->Graphics) {
    RveFillRect (Context, 0, 0, Context->Width, Context->Height, mColorBg);
  } else {
    gST->ConOut->ClearScreen (gST->ConOut);
  }
}

STATIC
VOID
RveDrawChrome (
  IN RVE_CONTEXT  *Context,
  IN CONST CHAR16 *Title,
  IN CONST CHAR16 *Help
  )
{
  if (Context->Graphics) {
    RveFillRect (Context, 0, 0, Context->Width, 54, mColorHeader);
    if ((Context->Width > 20) && (Context->Height > 82)) {
      RveFillRect (Context, 10, 64, Context->Width - 20, Context->Height - 82, mColorPanel);
    }

    if (Context->Height > 36) {
      RveFillRect (Context, 0, Context->Height - 36, Context->Width, 36, mColorHeader);
    }
  }

  RveDrawText (Context, RVE_LEFT_MARGIN, RVE_TOP_MARGIN, Title, mColorText, mColorHeader);
  RveDrawText (Context, RVE_LEFT_MARGIN, Context->Height > 28 ? Context->Height - 28 : 0, Help, mColorMuted, mColorHeader);
}

STATIC
UINTN
RveVisibleRows (
  IN RVE_CONTEXT  *Context
  )
{
  if ((Context == NULL) || (Context->Height < 150)) {
    return 16;
  }

  return (Context->Height - 138) / RVE_LIST_ROW_HEIGHT;
}

STATIC
VOID
RveDrawList (
  IN RVE_CONTEXT   *Context,
  IN RVE_VARIABLE  *Variables,
  IN UINTN         Count,
  IN UINTN         Selected,
  IN UINTN         First,
  IN CONST CHAR16  *Message
  )
{
  UINTN   Row;
  UINTN   Index;
  UINTN   Rows;
  UINTN   Last;
  UINTN   Y;
  CHAR16  Line[220];
  CHAR16  Attrs[96];
  CHAR16  Guid[48];

  RveClear (Context);
  RveDrawChrome (
    Context,
    L"RuntimeVarEditor [LIST] - EFI runtime variable browser/editor",
    L"Up/Down/PgUp/PgDn select  Enter view  E edit byte  A attrs  D delete  R refresh  Q quit"
    );

  Rows = RveVisibleRows (Context);
  Last = Count == 0 ? 0 : First + Rows;
  if (Last > Count) {
    Last = Count;
  }

  UnicodeSPrint (
    Line,
    sizeof (Line),
    L"Runtime variables: %u    Selected: %u/%u    Showing: %u-%u    %s",
    (UINT32)Count,
    (UINT32)(Count == 0 ? 0 : Selected + 1),
    (UINT32)Count,
    (UINT32)(Count == 0 ? 0 : First + 1),
    (UINT32)Last,
    Message == NULL ? L"" : Message
    );
  RveDrawText (Context, RVE_LEFT_MARGIN, 64, Line, Count == 0 ? mColorWarn : mColorOk, mColorPanel);

  if (Count > 0) {
    RveGuidToString (&Variables[Selected].VendorGuid, Guid, ARRAY_SIZE (Guid));
    RveAttrsToString (Variables[Selected].Attributes, Attrs, ARRAY_SIZE (Attrs));
    UnicodeSPrint (Line, sizeof (Line), L"Selected: %s    Size: %u", Variables[Selected].Name, (UINT32)Variables[Selected].DataSize);
    RveDrawText (Context, RVE_LEFT_MARGIN, 88, Line, mColorWarn, mColorPanel);
    UnicodeSPrint (Line, sizeof (Line), L"GUID: %s    Attrs: %s", Guid, Attrs);
    RveDrawText (Context, RVE_LEFT_MARGIN, 110, Line, mColorMuted, mColorPanel);
  }

  if (Context->Graphics && (Context->Width > 28)) {
    RveFillRect (Context, 14, 138, Context->Width - 28, RVE_LIST_ROW_HEIGHT, mColorBg);
  }

  RveDrawText (Context, 24, 140, L"Name", mColorMuted, mColorBg);
  RveDrawText (Context, 320, 140, L"Attributes", mColorMuted, mColorBg);
  RveDrawText (Context, 600, 140, L"Size", mColorMuted, mColorBg);
  RveDrawText (Context, 690, 140, L"Vendor GUID", mColorMuted, mColorBg);

  for (Row = 0; Row < Rows; Row++) {
    Index = First + Row;
    if (Index >= Count) {
      break;
    }

    Y = 166 + Row * RVE_LIST_ROW_HEIGHT;
    if ((Index == Selected) && (Context->Width > 28)) {
      RveFillRect (Context, 14, Y - 2, Context->Width - 28, RVE_LIST_ROW_HEIGHT, mColorSelect);
    }

    RveAttrsToString (Variables[Index].Attributes, Attrs, ARRAY_SIZE (Attrs));
    RveGuidToString (&Variables[Index].VendorGuid, Guid, ARRAY_SIZE (Guid));
    RveDrawText (Context, 24, Y, Variables[Index].Name, mColorText, Index == Selected ? mColorSelect : mColorPanel);
    RveDrawText (Context, 320, Y, Attrs, mColorText, Index == Selected ? mColorSelect : mColorPanel);
    UnicodeSPrint (Line, sizeof (Line), L"%u", (UINT32)Variables[Index].DataSize);
    RveDrawText (Context, 600, Y, Line, mColorText, Index == Selected ? mColorSelect : mColorPanel);
    RveDrawText (Context, 690, Y, Guid, mColorText, Index == Selected ? mColorSelect : mColorPanel);
  }
}

STATIC
VOID
RveDrawListSelectionInfo (
  IN RVE_CONTEXT   *Context,
  IN RVE_VARIABLE  *Variables,
  IN UINTN         Count,
  IN UINTN         Selected,
  IN UINTN         First,
  IN UINTN         Rows,
  IN CONST CHAR16  *Message
  )
{
  UINTN   Last;
  CHAR16  Line[220];
  CHAR16  Attrs[96];
  CHAR16  Guid[48];

  Last = Count == 0 ? 0 : First + Rows;
  if (Last > Count) {
    Last = Count;
  }

  if (Context->Graphics && (Context->Width > 28)) {
    RveFillRect (Context, 14, 62, Context->Width - 28, 72, mColorPanel);
  }

  UnicodeSPrint (
    Line,
    sizeof (Line),
    L"Runtime variables: %u    Selected: %u/%u    Showing: %u-%u    %s",
    (UINT32)Count,
    (UINT32)(Count == 0 ? 0 : Selected + 1),
    (UINT32)Count,
    (UINT32)(Count == 0 ? 0 : First + 1),
    (UINT32)Last,
    Message == NULL ? L"" : Message
    );
  RveDrawText (Context, RVE_LEFT_MARGIN, 64, Line, Count == 0 ? mColorWarn : mColorOk, mColorPanel);

  if (Count > 0) {
    RveGuidToString (&Variables[Selected].VendorGuid, Guid, ARRAY_SIZE (Guid));
    RveAttrsToString (Variables[Selected].Attributes, Attrs, ARRAY_SIZE (Attrs));
    UnicodeSPrint (Line, sizeof (Line), L"Selected: %s    Size: %u", Variables[Selected].Name, (UINT32)Variables[Selected].DataSize);
    RveDrawText (Context, RVE_LEFT_MARGIN, 88, Line, mColorWarn, mColorPanel);
    UnicodeSPrint (Line, sizeof (Line), L"GUID: %s    Attrs: %s", Guid, Attrs);
    RveDrawText (Context, RVE_LEFT_MARGIN, 110, Line, mColorMuted, mColorPanel);
  }
}

STATIC
VOID
RveDrawListRow (
  IN RVE_CONTEXT   *Context,
  IN RVE_VARIABLE  *Variables,
  IN UINTN         Index,
  IN UINTN         First,
  IN UINTN         Selected
  )
{
  UINTN   Y;
  CHAR16  Line[48];
  CHAR16  Attrs[96];
  CHAR16  Guid[48];
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  RowColor;

  if (Index < First) {
    return;
  }

  Y = 166 + (Index - First) * RVE_LIST_ROW_HEIGHT;
  RowColor = (Index == Selected) ? mColorSelect : mColorPanel;
  if (Context->Graphics && (Context->Width > 28)) {
    RveFillRect (Context, 14, Y - 2, Context->Width - 28, RVE_LIST_ROW_HEIGHT, RowColor);
  }

  RveAttrsToString (Variables[Index].Attributes, Attrs, ARRAY_SIZE (Attrs));
  RveGuidToString (&Variables[Index].VendorGuid, Guid, ARRAY_SIZE (Guid));
  RveDrawText (Context, 24, Y, Variables[Index].Name, mColorText, RowColor);
  RveDrawText (Context, 320, Y, Attrs, mColorText, RowColor);
  UnicodeSPrint (Line, sizeof (Line), L"%u", (UINT32)Variables[Index].DataSize);
  RveDrawText (Context, 600, Y, Line, mColorText, RowColor);
  RveDrawText (Context, 690, Y, Guid, mColorText, RowColor);
}

STATIC
VOID
RveRedrawListSelection (
  IN RVE_CONTEXT   *Context,
  IN RVE_VARIABLE  *Variables,
  IN UINTN         Count,
  IN UINTN         OldSelected,
  IN UINTN         NewSelected,
  IN UINTN         First,
  IN CONST CHAR16  *Message
  )
{
  UINTN  Rows;

  if ((Count == 0) || (Variables == NULL)) {
    return;
  }

  Rows = RveVisibleRows (Context);
  RveDrawListSelectionInfo (Context, Variables, Count, NewSelected, First, Rows, Message);
  if ((OldSelected >= First) && (OldSelected < First + Rows)) {
    RveDrawListRow (Context, Variables, OldSelected, First, NewSelected);
  }

  if ((NewSelected != OldSelected) && (NewSelected >= First) && (NewSelected < First + Rows)) {
    RveDrawListRow (Context, Variables, NewSelected, First, NewSelected);
  }
}

STATIC
EFI_STATUS
RveReadLine (
  IN  RVE_CONTEXT  *Context,
  IN  CONST CHAR16 *Prompt,
  OUT CHAR16       *Buffer,
  IN  UINTN        BufferChars
  )
{
  EFI_INPUT_KEY  Key;
  UINTN          Len;
  CHAR16         Line[160];
  UINTN          EventIndex;

  if ((Buffer == NULL) || (BufferChars == 0)) {
    return EFI_INVALID_PARAMETER;
  }

  Buffer[0] = L'\0';
  Len       = 0;

  while (TRUE) {
    UnicodeSPrint (Line, sizeof (Line), L"%s%s_", Prompt, Buffer);
    if (Context->Graphics && (Context->Width > 32) && (Context->Height > 58)) {
      RveFillRect (Context, 16, Context->Height - 58, Context->Width - 32, 24, mColorBg);
    }

    RveDrawText (Context, RVE_LEFT_MARGIN, Context->Height - 56, Line, mColorWarn, mColorBg);
    gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &EventIndex);
    if (EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
      continue;
    }

    if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
      return EFI_SUCCESS;
    }

    if (Key.UnicodeChar == CHAR_BACKSPACE) {
      if (Len > 0) {
        Len--;
        Buffer[Len] = L'\0';
      }
      continue;
    }

    if (Key.ScanCode == SCAN_ESC) {
      return EFI_ABORTED;
    }

    if ((Key.UnicodeChar >= L' ') && (Len + 1 < BufferChars)) {
      Buffer[Len++] = Key.UnicodeChar;
      Buffer[Len]   = L'\0';
    }
  }
}

STATIC
BOOLEAN
RveParseHexUintn (
  IN  CONST CHAR16  *Text,
  OUT UINTN         *Value
  )
{
  UINTN   Index;
  UINTN   Result;
  CHAR16  Ch;

  if ((Text == NULL) || (Value == NULL) || (Text[0] == L'\0')) {
    return FALSE;
  }

  Index = 0;
  if ((Text[0] == L'0') && ((Text[1] == L'x') || (Text[1] == L'X'))) {
    Index = 2;
  }

  Result = 0;
  for (; Text[Index] != L'\0'; Index++) {
    Ch = Text[Index];
    if (Result > (MAX_UINTN >> 4)) {
      return FALSE;
    }

    Result <<= 4;
    if ((Ch >= L'0') && (Ch <= L'9')) {
      Result |= Ch - L'0';
    } else if ((Ch >= L'a') && (Ch <= L'f')) {
      Result |= Ch - L'a' + 10;
    } else if ((Ch >= L'A') && (Ch <= L'F')) {
      Result |= Ch - L'A' + 10;
    } else {
      return FALSE;
    }
  }

  *Value = Result;
  return TRUE;
}

STATIC
BOOLEAN
RveConfirm (
  IN RVE_CONTEXT   *Context,
  IN CONST CHAR16  *Prompt
  )
{
  EFI_INPUT_KEY  Key;
  UINTN          EventIndex;

  if (Context->Graphics && (Context->Width > 32) && (Context->Height > 58)) {
    RveFillRect (Context, 16, Context->Height - 58, Context->Width - 32, 24, mColorBg);
  }

  RveDrawText (Context, RVE_LEFT_MARGIN, Context->Height - 56, Prompt, mColorDanger, mColorBg);
  while (TRUE) {
    gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &EventIndex);
    if (EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
      continue;
    }

    if ((Key.UnicodeChar == L'y') || (Key.UnicodeChar == L'Y')) {
      return TRUE;
    }

    if ((Key.UnicodeChar == L'n') || (Key.UnicodeChar == L'N') || (Key.ScanCode == SCAN_ESC)) {
      return FALSE;
    }
  }
}

STATIC
EFI_STATUS
RveEditByte (
  IN RVE_CONTEXT   *Context,
  IN RVE_VARIABLE  *Variable,
  OUT CHAR16       *Message,
  IN UINTN         MessageChars
  )
{
  EFI_STATUS  Status;
  UINT8       *Data;
  UINTN       DataSize;
  UINT32      Attributes;
  UINTN       Offset;
  UINTN       ByteValue;
  CHAR16      Input[32];
  CHAR16      Prompt[96];
  CHAR16      Confirm[160];
  UINT8       OldValue;

  Status = RveLoadVariableData (Variable, &Data, &DataSize, &Attributes);
  if (EFI_ERROR (Status)) {
    UnicodeSPrint (Message, MessageChars * sizeof (CHAR16), L"Read failed: %r", Status);
    return Status;
  }

  if (DataSize == 0) {
    FreePool (Data);
    UnicodeSPrint (Message, MessageChars * sizeof (CHAR16), L"Empty variable cannot be byte-edited.");
    return EFI_INVALID_PARAMETER;
  }

  if (RveIsAuthenticatedVariable (Attributes)) {
    FreePool (Data);
    UnicodeSPrint (Message, MessageChars * sizeof (CHAR16), L"Authenticated variables are read-only in this tool.");
    return EFI_SECURITY_VIOLATION;
  }

  UnicodeSPrint (Prompt, sizeof (Prompt), L"Offset hex 0..0x%x: ", (UINT32)(DataSize - 1));
  Status = RveReadLine (Context, Prompt, Input, ARRAY_SIZE (Input));
  if (EFI_ERROR (Status) || !RveParseHexUintn (Input, &Offset) || (Offset >= DataSize)) {
    FreePool (Data);
    UnicodeSPrint (Message, MessageChars * sizeof (CHAR16), L"Edit aborted or offset out of range.");
    return EFI_ABORTED;
  }

  OldValue = Data[Offset];
  UnicodeSPrint (Prompt, sizeof (Prompt), L"Offset 0x%x current=%02x, new byte 00-ff: ", (UINT32)Offset, (UINT32)OldValue);
  Status = RveReadLine (Context, Prompt, Input, ARRAY_SIZE (Input));
  if (EFI_ERROR (Status) || !RveParseHexUintn (Input, &ByteValue) || (ByteValue > 0xFF)) {
    FreePool (Data);
    UnicodeSPrint (Message, MessageChars * sizeof (CHAR16), L"Edit aborted or byte invalid.");
    return EFI_ABORTED;
  }

  UnicodeSPrint (Confirm, sizeof (Confirm), L"Write %s offset 0x%x: %02x -> %02x ? y/N", Variable->Name, (UINT32)Offset, (UINT32)OldValue, (UINT32)ByteValue);
  if (!RveConfirm (Context, Confirm)) {
    FreePool (Data);
    UnicodeSPrint (Message, MessageChars * sizeof (CHAR16), L"Byte edit cancelled.");
    return EFI_ABORTED;
  }

  Data[Offset] = (UINT8)ByteValue;
  Status = gRT->SetVariable (Variable->Name, &Variable->VendorGuid, RveNormalWriteAttributes (Attributes), DataSize, Data);
  FreePool (Data);

  UnicodeSPrint (Message, MessageChars * sizeof (CHAR16), L"SetVariable byte edit: %r", Status);
  return Status;
}

STATIC
EFI_STATUS
RveEditAttributes (
  IN RVE_CONTEXT   *Context,
  IN RVE_VARIABLE  *Variable,
  OUT CHAR16       *Message,
  IN UINTN         MessageChars
  )
{
  EFI_STATUS     Status;
  UINT8          *Data;
  UINTN          DataSize;
  UINT32         Attributes;
  UINT32         NewAttributes;
  EFI_INPUT_KEY  Key;
  UINTN          EventIndex;
  CHAR16         Line[220];
  CHAR16         AttrText[128];
  CHAR16         OldAttrText[128];
  CHAR16         Confirm[180];

  Status = RveLoadVariableData (Variable, &Data, &DataSize, &Attributes);
  if (EFI_ERROR (Status)) {
    UnicodeSPrint (Message, MessageChars * sizeof (CHAR16), L"Read failed: %r", Status);
    return Status;
  }

  if (RveIsAuthenticatedVariable (Attributes)) {
    FreePool (Data);
    UnicodeSPrint (Message, MessageChars * sizeof (CHAR16), L"Authenticated variables are read-only in this tool.");
    return EFI_SECURITY_VIOLATION;
  }

  NewAttributes = RveNormalWriteAttributes (Attributes);
  while (TRUE) {
    RveClear (Context);
    RveDrawChrome (Context, L"RuntimeVarEditor [ATTRIBUTES] - attribute editor", L"N toggle NV  B toggle BS  T toggle RT  S save  Esc cancel");
    RveAttrsToString (NewAttributes, AttrText, ARRAY_SIZE (AttrText));
    RveAttrsToString (Attributes, OldAttrText, ARRAY_SIZE (OldAttrText));
    UnicodeSPrint (Line, sizeof (Line), L"Variable: %s", Variable->Name);
    RveDrawText (Context, RVE_LEFT_MARGIN, 78, Line, mColorText, mColorPanel);
    UnicodeSPrint (Line, sizeof (Line), L"Current: %s", OldAttrText);
    RveDrawText (Context, RVE_LEFT_MARGIN, 104, Line, mColorMuted, mColorPanel);
    UnicodeSPrint (Line, sizeof (Line), L"New:     %s", AttrText);
    RveDrawText (Context, RVE_LEFT_MARGIN, 128, Line, mColorWarn, mColorPanel);
    RveDrawText (Context, RVE_LEFT_MARGIN, 156, L"Warning: changing firmware variables can make firmware or OS boot paths unusable.", mColorDanger, mColorPanel);
    if ((Message != NULL) && (Message[0] != L'\0')) {
      RveDrawText (Context, RVE_LEFT_MARGIN, 184, Message, mColorWarn, mColorPanel);
    }

    gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &EventIndex);
    if (EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
      continue;
    }

    if (Key.ScanCode == SCAN_ESC) {
      FreePool (Data);
      UnicodeSPrint (Message, MessageChars * sizeof (CHAR16), L"Attribute edit cancelled.");
      return EFI_ABORTED;
    }

    switch (Key.UnicodeChar) {
      case L'n':
      case L'N':
        NewAttributes ^= EFI_VARIABLE_NON_VOLATILE;
        break;
      case L'b':
      case L'B':
        NewAttributes ^= EFI_VARIABLE_BOOTSERVICE_ACCESS;
        break;
      case L't':
      case L'T':
        NewAttributes ^= EFI_VARIABLE_RUNTIME_ACCESS;
        break;
      case L's':
      case L'S':
        if (((NewAttributes & EFI_VARIABLE_RUNTIME_ACCESS) != 0) && ((NewAttributes & EFI_VARIABLE_BOOTSERVICE_ACCESS) == 0)) {
          UnicodeSPrint (Message, MessageChars * sizeof (CHAR16), L"Invalid attrs: RT requires BS.");
          break;
        }

        UnicodeSPrint (Confirm, sizeof (Confirm), L"Save attrs for %s: 0x%08x -> 0x%08x ? y/N", Variable->Name, Attributes, NewAttributes);
        if (!RveConfirm (Context, Confirm)) {
          break;
        }

        Status = gRT->SetVariable (Variable->Name, &Variable->VendorGuid, RveNormalWriteAttributes (NewAttributes), DataSize, Data);
        FreePool (Data);
        UnicodeSPrint (Message, MessageChars * sizeof (CHAR16), L"SetVariable attributes: %r", Status);
        return Status;
      default:
        break;
    }
  }
}

STATIC
EFI_STATUS
RveDeleteVariable (
  IN RVE_CONTEXT   *Context,
  IN RVE_VARIABLE  *Variable,
  OUT CHAR16       *Message,
  IN UINTN         MessageChars
  )
{
  EFI_STATUS  Status;
  CHAR16      Guid[48];
  CHAR16      Attrs[128];
  CHAR16      Prompt[220];

  if (RveIsAuthenticatedVariable (Variable->Attributes)) {
    UnicodeSPrint (Message, MessageChars * sizeof (CHAR16), L"Authenticated variables are read-only in this tool.");
    return EFI_SECURITY_VIOLATION;
  }

  RveGuidToString (&Variable->VendorGuid, Guid, ARRAY_SIZE (Guid));
  RveAttrsToString (Variable->Attributes, Attrs, ARRAY_SIZE (Attrs));
  UnicodeSPrint (Prompt, sizeof (Prompt), L"Delete %s size=%u attrs=%s ? y/N", Variable->Name, (UINT32)Variable->DataSize, Attrs);
  RveDrawText (Context, RVE_LEFT_MARGIN, Context->Height > 84 ? Context->Height - 84 : 0, Guid, mColorDanger, mColorBg);
  if (!RveConfirm (Context, Prompt)) {
    UnicodeSPrint (Message, MessageChars * sizeof (CHAR16), L"Delete cancelled.");
    return EFI_ABORTED;
  }

  Status = gRT->SetVariable (Variable->Name, &Variable->VendorGuid, RveNormalWriteAttributes (Variable->Attributes), 0, NULL);
  UnicodeSPrint (Message, MessageChars * sizeof (CHAR16), L"Delete SetVariable: %r", Status);
  return Status;
}

STATIC
VOID
RveDrawDetails (
  IN RVE_CONTEXT   *Context,
  IN RVE_VARIABLE  *Variable,
  IN UINTN         PageOffset,
  IN CONST CHAR16  *Message
  )
{
  EFI_STATUS  Status;
  UINT8       *Data;
  UINTN       DataSize;
  UINT32      Attributes;
  CHAR16      Line[220];
  CHAR16      Attrs[128];
  CHAR16      Guid[48];
  UINTN       Offset;
  UINTN       Row;
  UINTN       Col;
  UINTN       Value;
  UINTN       Y;
  UINTN       PageEnd;
  CHAR16      Hex[72];
  CHAR16      Ascii[20];

  RveClear (Context);
  RveDrawChrome (Context, L"RuntimeVarEditor [DETAIL] - variable detail", L"Left/Right hex page  E edit byte  A attrs  D delete  Esc/list  Q quit");

  RveGuidToString (&Variable->VendorGuid, Guid, ARRAY_SIZE (Guid));
  RveAttrsToString (Variable->Attributes, Attrs, ARRAY_SIZE (Attrs));
  UnicodeSPrint (Line, sizeof (Line), L"Name: %s", Variable->Name);
  RveDrawText (Context, RVE_LEFT_MARGIN, 70, Line, mColorText, mColorPanel);
  UnicodeSPrint (Line, sizeof (Line), L"GUID: %s", Guid);
  RveDrawText (Context, RVE_LEFT_MARGIN, 94, Line, mColorText, mColorPanel);
  UnicodeSPrint (Line, sizeof (Line), L"Attrs: %s    Size: %u", Attrs, (UINT32)Variable->DataSize);
  RveDrawText (Context, RVE_LEFT_MARGIN, 118, Line, mColorText, mColorPanel);

  Status = RveLoadVariableData (Variable, &Data, &DataSize, &Attributes);
  if (EFI_ERROR (Status)) {
    UnicodeSPrint (Line, sizeof (Line), L"GetVariable failed: %r", Status);
    RveDrawText (Context, RVE_LEFT_MARGIN, 150, Line, mColorDanger, mColorPanel);
    return;
  }

  if (PageOffset >= DataSize) {
    PageOffset = 0;
  }

  PageEnd = DataSize == 0 ? 0 : PageOffset + RVE_HEX_PAGE_BYTES - 1;
  if ((DataSize != 0) && (PageEnd >= DataSize)) {
    PageEnd = DataSize - 1;
  }

  UnicodeSPrint (Line, sizeof (Line), L"Data page 0x%08x-0x%08x of 0x%08x    %s", (UINT32)PageOffset, (UINT32)PageEnd, (UINT32)DataSize, Message == NULL ? L"" : Message);
  RveDrawText (Context, RVE_LEFT_MARGIN, 146, Line, mColorWarn, mColorPanel);
  RveDrawText (Context, RVE_LEFT_MARGIN, 170, L"Offset    00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F    ASCII", mColorMuted, mColorPanel);

  for (Row = 0; Row < 16; Row++) {
    Offset = PageOffset + Row * 16;
    if (Offset >= DataSize) {
      break;
    }

    Hex[0] = L'\0';
    Ascii[0] = L'\0';
    for (Col = 0; Col < 16; Col++) {
      if ((Offset + Col) < DataSize) {
        Value = Data[Offset + Col];
        if (Col == 8) {
          UnicodeSPrint (Hex + StrLen (Hex), sizeof (Hex) - StrLen (Hex) * sizeof (CHAR16), L" ");
        }

        UnicodeSPrint (Hex + StrLen (Hex), sizeof (Hex) - StrLen (Hex) * sizeof (CHAR16), L"%02x ", (UINT32)Value);
        Ascii[Col] = ((Value >= 0x20) && (Value <= 0x7E)) ? (CHAR16)Value : L'.';
      } else {
        UnicodeSPrint (Hex + StrLen (Hex), sizeof (Hex) - StrLen (Hex) * sizeof (CHAR16), L"   ");
        Ascii[Col] = L' ';
      }
    }
    Ascii[16] = L'\0';

    UnicodeSPrint (Line, sizeof (Line), L"%08x  %-48s  |%s|", (UINT32)Offset, Hex, Ascii);
    Y = 194 + Row * RVE_LIST_ROW_HEIGHT;
    RveDrawText (Context, RVE_LEFT_MARGIN, Y, Line, mColorText, mColorPanel);
  }

  FreePool (Data);
}

STATIC
EFI_STATUS
RveRun (
  IN RVE_CONTEXT  *Context
  )
{
  EFI_STATUS     Status;
  RVE_VARIABLE   *Variables;
  UINTN          Count;
  UINTN          Selected;
  UINTN          First;
  UINTN          Rows;
  UINTN          OldSelected;
  UINTN          OldFirst;
  BOOLEAN        FastSelectionUpdate;
  BOOLEAN        SelectionKey;
  BOOLEAN        ScrollChanged;
  BOOLEAN        Detail;
  UINTN          DetailOffset;
  EFI_INPUT_KEY  Key;
  UINTN          EventIndex;
  CHAR16         Message[160];
  BOOLEAN        FullRedraw;

  Variables = NULL;
  Count     = 0;
  Selected  = 0;
  First     = 0;
  Detail    = FALSE;
  DetailOffset = 0;
  Message[0] = L'\0';
  FullRedraw = TRUE;

  Status = RveLoadVariables (&Variables, &Count, Message, ARRAY_SIZE (Message));
  if (EFI_ERROR (Status)) {
    UnicodeSPrint (Message, sizeof (Message), L"Load failed: %r", Status);
  }

  while (TRUE) {
    if (Count == 0) {
      Selected = 0;
      First    = 0;
      Detail   = FALSE;
    } else if (Selected >= Count) {
      Selected = Count - 1;
    }

    Rows = RveVisibleRows (Context);
    if (Selected < First) {
      First = Selected;
    }
    if ((Rows > 0) && (Selected >= (First + Rows))) {
      First = Selected - Rows + 1;
    }

    if (FullRedraw) {
      if (Detail && (Count > 0)) {
        RveDrawDetails (Context, &Variables[Selected], DetailOffset, Message);
      } else {
        RveDrawList (Context, Variables, Count, Selected, First, Message);
      }

      FullRedraw = FALSE;
    }

    gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &EventIndex);
    if (EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
      continue;
    }

    Message[0] = L'\0';

    if ((Key.UnicodeChar == L'q') || (Key.UnicodeChar == L'Q')) {
      break;
    }

    if (Key.ScanCode == SCAN_ESC) {
      if (Detail) {
        Detail = FALSE;
        FullRedraw = TRUE;
      } else {
        break;
      }
      continue;
    }

    if ((Key.UnicodeChar == CHAR_CARRIAGE_RETURN) && (Count > 0)) {
      Detail = TRUE;
      DetailOffset = 0;
      FullRedraw = TRUE;
      continue;
    }

    if ((Key.UnicodeChar == L'r') || (Key.UnicodeChar == L'R')) {
      RveFreeVariables (Variables, Count);
      Variables = NULL;
      Count     = 0;
      Status = RveLoadVariables (&Variables, &Count, Message, ARRAY_SIZE (Message));
      if (EFI_ERROR (Status)) {
        UnicodeSPrint (Message, sizeof (Message), L"Refresh failed: %r", Status);
      } else {
        UnicodeSPrint (Message, sizeof (Message), L"Refreshed.");
      }
      FullRedraw = TRUE;
      continue;
    }

    if (Count == 0) {
      continue;
    }

    OldSelected = Selected;
    OldFirst = First;
    SelectionKey = FALSE;
    ScrollChanged = FALSE;
    FastSelectionUpdate = FALSE;

    switch (Key.ScanCode) {
      case SCAN_UP:
        if (Selected > 0) {
          Selected--;
          DetailOffset = 0;
          SelectionKey = !Detail;
        }
        break;
      case SCAN_DOWN:
        if ((Selected + 1) < Count) {
          Selected++;
          DetailOffset = 0;
          SelectionKey = !Detail;
        }
        break;
      case SCAN_PAGE_UP:
        if (Selected > Rows) {
          Selected -= Rows;
        } else {
          Selected = 0;
        }
        DetailOffset = 0;
        FullRedraw = TRUE;
        break;
      case SCAN_PAGE_DOWN:
        if ((Selected + Rows) < Count) {
          Selected += Rows;
        } else {
          Selected = Count - 1;
        }
        DetailOffset = 0;
        FullRedraw = TRUE;
        break;
      case SCAN_LEFT:
        if (Detail && (DetailOffset >= RVE_HEX_PAGE_BYTES)) {
          DetailOffset -= RVE_HEX_PAGE_BYTES;
          FullRedraw = TRUE;
        }
        break;
      case SCAN_RIGHT:
        if (Detail && ((DetailOffset + RVE_HEX_PAGE_BYTES) < Variables[Selected].DataSize)) {
          DetailOffset += RVE_HEX_PAGE_BYTES;
          FullRedraw = TRUE;
        }
        break;
      default:
        break;
    }

    if (SelectionKey && (Selected != OldSelected)) {
      if (Selected < First) {
        First = Selected;
      }

      if ((Rows > 0) && (Selected >= (First + Rows))) {
        First = Selected - Rows + 1;
      }

      ScrollChanged = (BOOLEAN)(First != OldFirst);
      FastSelectionUpdate = (BOOLEAN)(!ScrollChanged && !Detail);
      if (FastSelectionUpdate) {
        RveRedrawListSelection (Context, Variables, Count, OldSelected, Selected, First, Message);
        FullRedraw = FALSE;
        continue;
      }

      FullRedraw = TRUE;
    }

    if ((Key.UnicodeChar == L'e') || (Key.UnicodeChar == L'E')) {
      Status = RveEditByte (Context, &Variables[Selected], Message, ARRAY_SIZE (Message));
      RveReadVariableInfo (&Variables[Selected]);
      Detail = TRUE;
      FullRedraw = TRUE;
    } else if ((Key.UnicodeChar == L'a') || (Key.UnicodeChar == L'A')) {
      Status = RveEditAttributes (Context, &Variables[Selected], Message, ARRAY_SIZE (Message));
      if (!EFI_ERROR (Status)) {
        RveFreeVariables (Variables, Count);
        Variables = NULL;
        Count     = 0;
        Status = RveLoadVariables (&Variables, &Count, Message, ARRAY_SIZE (Message));
        if (Selected >= Count && Count > 0) {
          Selected = Count - 1;
        }
        Detail = FALSE;
        FullRedraw = TRUE;
      } else {
        RveReadVariableInfo (&Variables[Selected]);
        Detail = TRUE;
        FullRedraw = TRUE;
      }
    } else if ((Key.UnicodeChar == L'd') || (Key.UnicodeChar == L'D')) {
      RveDeleteVariable (Context, &Variables[Selected], Message, ARRAY_SIZE (Message));
      RveFreeVariables (Variables, Count);
      Variables = NULL;
      Count     = 0;
      Status = RveLoadVariables (&Variables, &Count, Message, ARRAY_SIZE (Message));
      if (Selected >= Count && Count > 0) {
        Selected = Count - 1;
      }
      Detail = FALSE;
      FullRedraw = TRUE;
    }
  }

  RveFreeVariables (Variables, Count);
  return EFI_SUCCESS;
}

/**
  Application entry point.

  @param[in] ImageHandle  Firmware allocated image handle.
  @param[in] SystemTable  EFI system table.

  @retval EFI_SUCCESS     The user exited the editor.
  @retval Others          Initialization failed.
**/
EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  RVE_CONTEXT  Context;

  RveInitContext (&Context);
  if (!Context.Graphics) {
    Print (L"RuntimeVarEditor: GOP unavailable; using text fallback.\r\n");
  } else if (Context.Font == NULL) {
    Print (L"RuntimeVarEditor: HII Font unavailable; using text fallback.\r\n");
    Context.Graphics = FALSE;
  }

  Print (L"RuntimeVarEditor: runtime variable editing can affect boot and OS behavior.\r\n");
  Print (L"Use only in a VM, lab system, or controlled firmware shell.\r\n");
  gBS->Stall (1000 * 1000);

  return RveRun (&Context);
}
