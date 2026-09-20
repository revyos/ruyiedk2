// SPDX-License-Identifier: BSD-2-Clause-Patent
// TH1520 RGMII controllers and RTL8211 PHYs with DT-described MDIO wiring.
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
  // CSR clock / 124; within the RTL8211F MDC limit on TH1520.
  MdioWrite (Hw, MAC_MDIO_ADDRESS, (Hw->PhyAddress << 11) | (Register << 6) | 0x14 | BIT0);
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
  MdioWrite (Hw, MAC_MDIO_ADDRESS, (Hw->PhyAddress << 11) | (Register << 6) | 0x14 | BIT1 | BIT0);
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
GmacSetSpeed (GMAC_HARDWARE *Hw, UINTN Speed)
{
  UINT64 Rate;
  UINTN Divisor;
  switch (Speed)
  {
  case 1000:
    Rate = 125000000;
    break;
  case 100:
    Rate = 25000000;
    break;
  case 10:
    Rate = 2500000;
    break;
  default:
    return EFI_UNSUPPORTED;
  }
  Divisor = (UINTN)(Hw->Clock / Rate);
  if (!Divisor || Divisor > 255 || Divisor * Rate != Hw->Clock)
  {
    return EFI_UNSUPPORTED;
  }
  MmioWrite32 (Hw->Apb + 0x0c, 0);
  MmioWrite32 (Hw->Apb + 0x0c, BIT31 | (UINT32)Divisor);
  return EFI_SUCCESS;
}
EFI_STATUS
GmacPlatformInitialize (GMAC_HARDWARE *Hw)
{
  EFI_STATUS Status;
  UINT32 Config0, Config1, Divisor;
  UINT64 Pll;
  MmioOr32 (0xffef010150ULL, BIT12);
  MmioOr32 (0xffef010204ULL, BIT22 | BIT21 | (Hw->Index == 0 ? BIT19 : BIT26) |
                                 (Hw->MdioBase == 0xffe7070000ULL ? BIT19 : BIT26));
  Status = GmacConfigurePins (Hw);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  // LPi4A's GMAC1 PHY is reached through GMAC0's MDIO controller.
  if (Hw->MdioBase != Hw->Base)
  {
    UINTN MdioApb = Hw->MdioBase == 0xffe7070000ULL ? 0xffec003000ULL : 0xffec004000ULL;
    MmioWrite32 (MdioApb, BIT1 | BIT2 | BIT3 | BIT4 | BIT5);
  }
  // GMAC AXI clock is GMAC_PLL divided by AXI4_CPUSYS2_ACLK.
  Config0 = MmioRead32 (0xffef010020ULL);
  Config1 = MmioRead32 (0xffef010024ULL);
  Divisor = (Config0 & 63) * ((Config0 >> 20) & 7) * ((Config0 >> 24) & 7);
  if (!Divisor)
  {
    return EFI_DEVICE_ERROR;
  }
  Pll = LShiftU64 ((Config0 >> 8) & 4095, 24);
  if (!(Config1 & BIT24))
  {
    Pll += Config1 & 0xffffff;
  }
  Pll = (Config1 & BIT30) ? 24000000
                          : DivU64x32 (RShiftU64 (MultU64x32 (Pll, 24000000), 24), Divisor);
  Divisor = MmioRead32 (0xffef010134ULL) & 7;
  if (!Divisor)
  {
    return EFI_DEVICE_ERROR;
  }
  Hw->Clock = DivU64x32 (Pll, Divisor);
  MmioWrite32 (Hw->Apb + 0x1c, 1); // RGMII
  MmioWrite32 (Hw->Apb + 0x20, 0); // MAC drives TX clock
  MmioAnd32 (Hw->Apb + 4, ~31U);
  MmioAnd32 (Hw->Apb + 8, ~31U);
  MmioWrite32 (Hw->Apb + 0x18, 1);
  MmioWrite32 (Hw->Apb, BIT1 | BIT2 | BIT3 | BIT4 | BIT5);
  return GmacSetSpeed (Hw, 1000);
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
  DEBUG ((DEBUG_INFO, "TH1520 GMAC: PHY ID %08x, AXI clock %Lu\n", Id, Hw->Clock));
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
