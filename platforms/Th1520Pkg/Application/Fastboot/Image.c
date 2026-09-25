// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "Image.h"
#include <Library/BaseLib.h>
#include <Library/BaseCryptLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/FdtLib.h>
#include <Library/MemoryAllocationLib.h>

STATIC BOOLEAN
BoardMatches (CONST VOID *Data, UINTN Bytes, CONST CHAR8 *Compatible)
{
  VOID *Fdt;
  CONST CHAR8 *Text;
  INT32 Len, Position = 0;
  UINTN ExpectedLength = AsciiStrLen (Compatible);
  BOOLEAN Match = FALSE;
  if (Bytes < 40) return FALSE;
  // FIT external data is only four-byte aligned; libfdt requires eight.
  Fdt = AllocateCopyPool (Bytes, Data);
  if (Fdt == NULL) return FALSE;
  if (FdtCheckHeader (Fdt) || FdtTotalSize (Fdt) > Bytes) goto Out;
  Text = FdtGetProp (Fdt, 0, "compatible", &Len);
  if (Text == NULL || Len <= 0) goto Out;
  while (Position < Len)
  {
    UINTN Length = AsciiStrnLenS (Text + Position, Len - Position);
    if (Length == Len - Position) { Match = FALSE; goto Out; }
    if (Length == ExpectedLength && !CompareMem (Text + Position, Compatible, Length))
      Match = TRUE;
    Position += Length + 1;
  }
Out:
  FreePool (Fdt);
  return Match;
}

// Validate the external-data FIT before the first erase, including every hash.
BOOLEAN
Th1520ValidImage (CONST UINT8 *Image, UINTN Size, CONST TH1520_IMAGE_POLICY *Policy)
{
  CONST VOID *Fdt;
  CONST CHAR8 *Text;
  UINTN ExpectedLength;
  CONST UINT32 *Position, *DataSize;
  CONST CHAR8 *Algo;
  CONST VOID *Expected;
  INT32 Images, Node, Hash, Len, Config, Configurations, Dtb;
  UINT32 Offset, Bytes;
  UINT8 Digest[32];
  if (Image == NULL || Policy == NULL || Size <= 0x40000 + 40 || Size > Policy->MaximumSize)
  {
    return FALSE;
  }
  Fdt = Image + 0x40000;
  if (FdtCheckHeader (Fdt) || FdtTotalSize (Fdt) > Size - 0x40000)
  {
    return FALSE;
  }
  Configurations = FdtPathOffset (Fdt, "/configurations");
  if (Configurations < 0)
  {
    return FALSE;
  }
  Text = FdtGetProp (Fdt, Configurations, "default", &Len);
  ExpectedLength = AsciiStrSize (Policy->Configuration);
  if (Text == NULL || Len != ExpectedLength || CompareMem (Text, Policy->Configuration, Len))
  {
    return FALSE;
  }
  Config = FdtSubnodeOffset (Fdt, Configurations, Policy->Configuration);
  if (Config < 0 || FdtGetProp (Fdt, Config, "description", &Len) == NULL || Len <= 1)
  {
    return FALSE;
  }
  Text = FdtGetProp (Fdt, Config, "firmware-storage", &Len);
  if (Text != NULL)
  {
    ExpectedLength = AsciiStrSize (Policy->Medium);
    if (Len != ExpectedLength || CompareMem (Text, Policy->Medium, Len))
    {
      return FALSE;
    }
  }
  else if (AsciiStrCmp (Policy->Medium, "spi-nor"))
  {
    // Legacy Meles images predate this property; boot0 must reject RAM SPLs.
    return FALSE;
  }
  Images = FdtPathOffset (Fdt, "/images");
  if (Images < 0)
  {
    return FALSE;
  }
  if (FdtSubnodeOffset (Fdt, Images, "opensbi") < 0 || FdtSubnodeOffset (Fdt, Images, "edk2") < 0 ||
      FdtSubnodeOffset (Fdt, Images, "fdt") < 0 || FdtSubnodeOffset (Fdt, Images, "aon") < 0)
  {
    return FALSE;
  }
  Dtb = FdtSubnodeOffset (Fdt, Images, "fdt");
  FdtForEachSubnode (Node, Fdt, Images)
  {
    Position = FdtGetProp (Fdt, Node, "data-position", &Len);
    if (!Position || Len != 4)
    {
      return FALSE;
    }
    Offset = Fdt32ToCpu (*Position);
    DataSize = FdtGetProp (Fdt, Node, "data-size", &Len);
    if (!DataSize || Len != 4)
    {
      return FALSE;
    }
    Bytes = Fdt32ToCpu (*DataSize);
    if (!Bytes || Offset < FdtTotalSize (Fdt) || Offset > Size - 0x40000 ||
        Bytes > Size - 0x40000 - Offset)
    {
      return FALSE;
    }
    if (Node == Dtb && !BoardMatches (Image + 0x40000 + Offset, Bytes, Policy->Compatible))
      return FALSE;
    Hash = FdtSubnodeOffset (Fdt, Node, "hash");
    if (Hash < 0)
    {
      return FALSE;
    }
    Algo = FdtGetProp (Fdt, Hash, "algo", &Len);
    if (!Algo || Len != 7 || CompareMem (Algo, "sha256", 7))
    {
      return FALSE;
    }
    Expected = FdtGetProp (Fdt, Hash, "value", &Len);
    if (!Expected || Len != 32)
    {
      return FALSE;
    }
    if (!Sha256HashAll (Image + 0x40000 + Offset, Bytes, Digest) ||
        CompareMem (Expected, Digest, 32))
    {
      return FALSE;
    }
  }
  return Node == -FDT_ERR_NOTFOUND;
}
