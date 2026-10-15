/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include "../Include/Decode.h"
int main(void) {
  UINT64 End;
  UINT8 Data[] = {0x78,0x56,0x34,0x12,0xef,0xcd,0xab,0x90};
  assert(Le32(Data) == 0x12345678);
  assert(Le64(Data) == 0x90abcdef12345678ULL);
  assert(DmaEnd(0x1000, 16, &End) && End == 0x1010);
  assert(!DmaEnd(1, 0, &End));
  assert(!DmaEnd(UINT64_MAX, 1, &End));
  assert(!DmaEnd(UINT64_MAX - 15, 16, &End));
  assert(DmaEnd(UINT64_MAX - 16, 16, &End) && End == UINT64_MAX);
  assert(SafeRamType(1, 8)); assert(SafeRamType(2, 8));
  assert(SafeRamType(3, 8)); assert(SafeRamType(4, 8)); assert(SafeRamType(7, 8));
  for (unsigned i = 0; i < 16; i++) {
    if (i != 1 && i != 2 && i != 3 && i != 4 && i != 7) assert(!SafeRamType(i, 8));
  }
  assert(!SafeRamType(7, 8 | 0x8000000000000000ULL));
  assert(!SafeRamType(7, 8 | 0x2000)); assert(!SafeRamType(7, 0));
  assert(RamContains(0x1000, 2, 0x1000, 0x2000));
  assert(!RamContains(0x1000, 2, 0x1000, 0x2001));
  assert(!RamContains(0x1000, 2, 0xfff, 16));
  assert(!RamContains(0x1000, 2, UINT64_MAX - 8, 16));
  assert(!RamContains(0, 1, 0, 16));
  assert(!RamContains(0, 1ULL << 52, 0x1000, 16));
  assert(!RamContains(UINT64_MAX - 4095, 1, UINT64_MAX - 16, 16));
  assert(RtlLength(0xc00005ee, TRUE) == 1518);
  assert(RtlLength(0x3fff, TRUE) == 16383);
  assert(RtlLength(0xffff, TRUE) == 16383);
  assert(RtlLength(0xc000ffff, FALSE) == 65535);
  assert(RtlLength(0xf0000000, FALSE) == 0);
  puts("PASS: endian, end-exclusive/overflow, safe RAM types/attributes/bounds, Realtek RX/TX masks");
  return 0;
}
