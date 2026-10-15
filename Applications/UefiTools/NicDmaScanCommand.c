/** @file
  Reuse the standalone read-only NIC scanner in the unified application.
  SPDX-License-Identifier: Apache-2.0
**/
#define ShellAppMain NicDmaScanMain
#include "../NicDmaScan/Decode.c"
#include "../NicDmaScan/NicDmaScan.c"
