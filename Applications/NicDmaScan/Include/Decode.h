/* SPDX-License-Identifier: Apache-2.0 */
#ifndef NIC_DMA_DECODE_H
#define NIC_DMA_DECODE_H
#ifdef HOST_TEST
#include <stdint.h>
typedef uint8_t UINT8;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef int BOOLEAN;
#define TRUE 1
#define FALSE 0
#else
#include <Uefi.h>
#endif
#define SNAPSHOT_DESCRIPTORS 256U
BOOLEAN DmaEnd(UINT64 Base, UINT64 Length, UINT64 *End);
UINT64 Le64(const UINT8 *P);
UINT32 Le32(const UINT8 *P);
BOOLEAN SafeRamType(UINT32 Type, UINT64 Attributes);
BOOLEAN RamContains(UINT64 Start, UINT64 Pages, UINT64 Base, UINT64 Length);
UINT32 RtlLength(UINT32 Opts1, BOOLEAN Rx);
#endif
