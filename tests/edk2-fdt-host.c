// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host adapters for the production FdtLib and BaseLib calls.
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/FdtLib.h>
#include <libfdt.h>
#include <stdlib.h>
#include <string.h>

UINT32 EFIAPI
ReadUnaligned32 (CONST VOID *P)
{
  UINT32 V;
  memcpy (&V, P, 4);
  return V;
}
UINT64 EFIAPI
ReadUnaligned64 (CONST VOID *P)
{
  UINT64 V;
  memcpy (&V, P, 8);
  return V;
}
UINT32 EFIAPI
SwapBytes32 (UINT32 V)
{
  return __builtin_bswap32 (V);
}
UINT64 EFIAPI
SwapBytes64 (UINT64 V)
{
  return __builtin_bswap64 (V);
}
UINT64 EFIAPI
LShiftU64 (UINT64 V, UINTN N)
{
  return V << N;
}
UINT64 EFIAPI
RShiftU64 (UINT64 V, UINTN N)
{
  return V >> N;
}
UINT64 EFIAPI
MultU64x32 (UINT64 V, UINT32 N)
{
  return V * N;
}
UINT64 EFIAPI
DivU64x32 (UINT64 V, UINT32 N)
{
  return V / N;
}
INTN EFIAPI
CompareMem (CONST VOID *A, CONST VOID *B, UINTN N)
{
  return memcmp (A, B, N);
}
INTN EFIAPI
AsciiStrCmp (CONST CHAR8 *A, CONST CHAR8 *B)
{
  return strcmp (A, B);
}
INTN EFIAPI
AsciiStrnCmp (CONST CHAR8 *A, CONST CHAR8 *B, UINTN N)
{
  return strncmp (A, B, N);
}
UINTN EFIAPI
AsciiStrLen (CONST CHAR8 *S)
{
  return strlen (S);
}
UINTN EFIAPI
AsciiStrnLenS (CONST CHAR8 *S, UINTN N)
{
  return strnlen (S, N);
}
EFI_STATUS EFIAPI
AsciiStrDecimalToUintnS (CONST CHAR8 *S, CHAR8 **End, UINTN *Value)
{
  *Value = strtoull (S, End, 10);
  return *End == S ? EFI_INVALID_PARAMETER : EFI_SUCCESS;
}
CONST VOID *EFIAPI
FdtGetProp (CONST VOID *F, INT32 N, CONST CHAR8 *P, INT32 *L)
{
  return fdt_getprop (F, N, P, L);
}
INT32 EFIAPI
FdtNodeOffsetByCompatible (CONST VOID *F, INT32 N, CONST CHAR8 *S)
{
  return fdt_node_offset_by_compatible (F, N, S);
}
INT32 EFIAPI
FdtNodeOffsetByPhandle (CONST VOID *F, UINT32 P)
{
  return fdt_node_offset_by_phandle (F, P);
}
INT32 EFIAPI
FdtParentOffset (CONST VOID *F, INT32 N)
{
  return fdt_parent_offset (F, N);
}
INT32 EFIAPI
FdtFirstSubnode (CONST VOID *F, INT32 N)
{
  return fdt_first_subnode (F, N);
}
INT32 EFIAPI
FdtNextSubnode (CONST VOID *F, INT32 N)
{
  return fdt_next_subnode (F, N);
}
INT32 EFIAPI
FdtNextNode (CONST VOID *F, INT32 N, INT32 *D)
{
  return fdt_next_node (F, N, D);
}
INT32 EFIAPI
FdtStringListSearch (CONST VOID *F, INT32 N, CONST CHAR8 *P,
                     CONST CHAR8 *S)
{
  return fdt_stringlist_search (F, N, P, S);
}
INT32 EFIAPI
FdtAddressCells (CONST VOID *F, INT32 N)
{
  return fdt_address_cells (F, N);
}
INT32 EFIAPI
FdtSizeCells (CONST VOID *F, INT32 N)
{
  return fdt_size_cells (F, N);
}
INT32 EFIAPI
FdtCheckHeader (CONST VOID *F)
{
  return fdt_check_header (F);
}
INT32 EFIAPI
FdtPathOffset (CONST VOID *F, CONST CHAR8 *Path)
{
  return fdt_path_offset (F, Path);
}
