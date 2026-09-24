/** @file
  Reuse the standalone tool in the unified UEFI application.
  Copyright (c) 2026, MarsDoge. All rights reserved.<BR>
  SPDX-License-Identifier: Apache-2.0
**/

#define UefiMain  PciOptionRomInfoMain
#include "../PciOptionRomInfo/PciOptionRomInfo.c"
