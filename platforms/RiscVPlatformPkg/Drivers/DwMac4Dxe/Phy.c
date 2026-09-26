// SPDX-License-Identifier: BSD-2-Clause-Patent
// Clause 22 MDIO and RTL8211 RGMII PHY support for DWMAC4/5.
#include "Gmac.h"

STATIC UINT32
MdioRead (GMAC_HARDWARE *Hw, UINTN Offset)
{
  return MmioRead32 (Hw->MdioBase + Offset);
}
STATIC VOID
MdioWrite (GMAC_HARDWARE *Hw, UINTN Offset, UINT32 Value)
{
  MmioWrite32 (Hw->MdioBase + Offset, Value);
}

STATIC EFI_STATUS
MdioWait (GMAC_HARDWARE *Hw)
{
  UINTN Retry;
  for (Retry = 0; Retry < 10000; Retry++)
  {
    if (!(MdioRead (Hw, MAC_MDIO_ADDRESS) & BIT0))
    {
      return EFI_SUCCESS;
    }
    MicroSecondDelay (10);
  }
  return EFI_TIMEOUT;
}
STATIC EFI_STATUS
PhyRead (GMAC_HARDWARE *Hw, UINTN Register, UINT16 *Value)
{
  EFI_STATUS Status = MdioWait (Hw);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  // The platform supplies a CSR clock no faster than 500 MHz.
  MdioWrite (Hw, MAC_MDIO_ADDRESS,
             (Hw->PhyAddress << 21) | (Register << 16) | (6 << 8)
                 | (3 << 2) | BIT0);
  Status = MdioWait (Hw);
  if (!EFI_ERROR (Status))
  {
    *Value = (UINT16)MdioRead (Hw, MAC_MDIO_DATA);
  }
  return Status;
}
STATIC EFI_STATUS
PhyWrite (GMAC_HARDWARE *Hw, UINTN Register, UINT16 Value)
{
  EFI_STATUS Status = MdioWait (Hw);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  MdioWrite (Hw, MAC_MDIO_DATA, Value);
  MdioWrite (Hw, MAC_MDIO_ADDRESS,
             (Hw->PhyAddress << 21) | (Register << 16) | (6 << 8)
                 | (1 << 2) | BIT0);
  return MdioWait (Hw);
}
STATIC EFI_STATUS
PhySetBits (GMAC_HARDWARE *Hw, UINTN Register, UINT16 Bits, BOOLEAN Enable)
{
  UINT16 Value;
  EFI_STATUS Status = PhyRead (Hw, Register, &Value);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  return PhyWrite (Hw, Register, Enable ? Value | Bits : Value & ~Bits);
}
EFI_STATUS
GmacPhyInitialize (GMAC_HARDWARE *Hw)
{
  EFI_STATUS Status;
  UINT16 Id1, Id2, Value;
  UINTN Retry;
  UINT32 Id;
  Status = PhyWrite (Hw, 31, 0);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = PhyRead (Hw, 2, &Id1);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = PhyRead (Hw, 3, &Id2);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Id = ((UINT32)Id1 << 16) | Id2;
  DEBUG (
      (DEBUG_INFO, "DWMAC: PHY ID %08x, CSR clock %Lu\n", Id, Hw->Clock));
  if ((Id & 0xffffff) != 0x1cc916 && (Id & 0xffffff) != 0x1cc878)
  {
    return EFI_UNSUPPORTED;
  }
  Status = PhyWrite (Hw, 0, BIT15);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  for (Retry = 0; Retry < 1000; Retry++)
  {
    Status = PhyRead (Hw, 0, &Value);
    if (EFI_ERROR (Status))
    {
      return Status;
    }
    if (!(Value & BIT15))
    {
      break;
    }
    MicroSecondDelay (1000);
  }
  if (Retry == 1000)
  {
    return EFI_TIMEOUT;
  }
  Status = PhyWrite (Hw, 31, 0xd08);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = PhySetBits (Hw, 0x11, BIT8, Hw->TxDelay);
  if (!EFI_ERROR (Status))
  {
    Status = PhySetBits (Hw, 0x15, BIT3, Hw->RxDelay);
  }
  if (EFI_ERROR (Status))
  {
    PhyWrite (Hw, 31, 0);
    return Status;
  }
  Status = PhyWrite (Hw, 31, 0xd04);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = PhyWrite (Hw, 0x10, 0x617f); // Link / activity LEDs
  if (EFI_ERROR (Status))
  {
    PhyWrite (Hw, 31, 0);
    return Status;
  }
  Status = PhyWrite (Hw, 31, 0);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  // Advertise 10/100 and 1000BASE-T full duplex; no pause offload.
  Status = PhyWrite (Hw, 4, BIT0 | BIT6 | BIT8);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = PhyWrite (Hw, 9, BIT9);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  return PhyWrite (Hw, 0, BIT12 | BIT9);
}
EFI_STATUS
GmacLink (GMAC_HARDWARE *Hw, BOOLEAN *Up, UINTN *Speed, BOOLEAN *Duplex)
{
  EFI_STATUS Status, Restore;
  UINT16 Value;
  *Up = FALSE;
  Status = PhyWrite (Hw, 31, 0xa43);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = PhyRead (Hw, 0x1a, &Value);
  Restore = PhyWrite (Hw, 31, 0);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  if (EFI_ERROR (Restore))
  {
    return Restore;
  }
  if (!(Value & BIT2))
  {
    return EFI_SUCCESS;
  }
  switch ((Value >> 4) & 3)
  {
  case 0:
    *Speed = 10;
    break;
  case 1:
    *Speed = 100;
    break;
  case 2:
    *Speed = 1000;
    break;
  default:
    return EFI_DEVICE_ERROR;
  }
  *Duplex = (Value & BIT3) != 0;
  *Up = TRUE;
  return EFI_SUCCESS;
}
