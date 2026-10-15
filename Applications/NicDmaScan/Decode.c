/* SPDX-License-Identifier: Apache-2.0 */
#include "Include/Decode.h"
BOOLEAN DmaEnd(UINT64 Base, UINT64 Length, UINT64 *End) {
  if (Length == 0 || Base > (~(UINT64)0) - Length) { return FALSE; }
  *End = Base + Length;
  return TRUE;
}
UINT32 Le32(const UINT8 *P) {
  return (UINT32)P[0] | ((UINT32)P[1] << 8) |
         ((UINT32)P[2] << 16) | ((UINT32)P[3] << 24);
}
UINT64 Le64(const UINT8 *P) {
  return (UINT64)Le32(P) | ((UINT64)Le32(P + 4) << 32);
}
BOOLEAN SafeRamType(UINT32 Type, UINT64 Attributes) {
  /* EFI loader code/data, boot services code/data, conventional memory only.
     Reject runtime, read-protect, and missing coherent WB CPU mapping. */
  return (Type == 1 || Type == 2 || Type == 3 || Type == 4 || Type == 7) &&
         ((Attributes & (0x8000000000000000ULL | 0x2000ULL)) == 0) &&
         ((Attributes & 8) != 0);
}
BOOLEAN RamContains(UINT64 Start, UINT64 Pages, UINT64 Base, UINT64 Length) {
  UINT64 RegionEnd, End;
  if (Base == 0 || Pages > ((~(UINT64)0) >> 12) ||
      !DmaEnd(Start, Pages << 12, &RegionEnd) ||
      !DmaEnd(Base, Length, &End)) { return FALSE; }
  return Base >= Start && End <= RegionEnd;
}
UINT32 RtlLength(UINT32 Opts1, BOOLEAN Rx) {
  return Opts1 & (Rx ? 0x3fffU : 0xffffU);
}
