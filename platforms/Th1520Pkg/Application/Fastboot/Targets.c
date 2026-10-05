// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "Targets.h"
#include <Library/BaseLib.h>

STATIC BOOLEAN
HexNumber (CONST CHAR8 **Text, UINT64 *Value)
{
  CONST CHAR8 *P = *Text;
  UINTN Digits = 0, Digit;
  *Value = 0;
  if (P[0] == '0' && (P[1] == 'x' || P[1] == 'X')) P += 2;
  while (*P && *P != ':')
  {
    if (*P >= '0' && *P <= '9') Digit = *P - '0';
    else if (*P >= 'a' && *P <= 'f') Digit = *P - 'a' + 10;
    else if (*P >= 'A' && *P <= 'F') Digit = *P - 'A' + 10;
    else return FALSE;
    if (*Value > (MAX_UINT64 - Digit) / 16) return FALSE;
    *Value = *Value * 16 + Digit;
    P++;
    Digits++;
  }
  *Text = P;
  return Digits != 0;
}

BOOLEAN
Th1520ParseStorageRange (CONST CHAR8 *Name, TH1520_STORAGE_RANGE *Range)
{
  STATIC CONST struct { CONST CHAR8 *Name; TH1520_FLASH_TARGET Target; } Names[] = {
    {"spi", Th1520TargetSpi}, {"emmc0", Th1520TargetEmmc},
    {"emmc0boot0", Th1520TargetEmmcBoot0}, {"emmc0boot1", Th1520TargetEmmcBoot1}
  };
  UINTN I, Length;
  if (Name == NULL || Range == NULL) return FALSE;
  Range->Target = Th1520TargetNone;
  Range->Offset = Range->Length = 0;
  Range->HasLength = FALSE;
  for (I = 0; I < ARRAY_SIZE (Names); I++)
  {
    Length = AsciiStrLen (Names[I].Name);
    if (!AsciiStrnCmp (Name, Names[I].Name, Length) &&
        (Name[Length] == 0 || Name[Length] == ':'))
    {
      Range->Target = Names[I].Target;
      Name += Length;
      break;
    }
  }
  if (Range->Target == Th1520TargetNone) return FALSE;
  if (*Name == 0) return TRUE;
  Name++;
  if (!HexNumber (&Name, &Range->Offset)) return FALSE;
  if (*Name == 0) return TRUE;
  Name++;
  if (!HexNumber (&Name, &Range->Length) || *Name != 0) return FALSE;
  Range->HasLength = TRUE;
  return TRUE;
}

UINT64
Th1520TargetCapacity (TH1520_FLASH_TARGET Target, UINT64 UserSize, UINT64 BootSize)
{
  if (Target == Th1520TargetSpi) return SIZE_8MB;
  if (Target == Th1520TargetEmmc) return UserSize;
  if (Target == Th1520TargetEmmcBoot0 || Target == Th1520TargetEmmcBoot1)
    return BootSize;
  return 0;
}
