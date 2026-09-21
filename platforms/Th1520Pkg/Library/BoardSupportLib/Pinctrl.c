// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "BoardFdt.h"

EFI_STATUS
PadController (CONST VOID *Fdt, INT32 Node, UINTN *Base, UINT32 *Group)
{
  UINTN Size;
  if (!Th1520FdtEnabled (Fdt, Node) ||
      !Compatible (Fdt, Node, "thead,th1520-pinctrl") ||
      EFI_ERROR (ReadCell (Fdt, Node, "thead,pad-group", Group)) ||
      EFI_ERROR (Th1520FdtReg (Fdt, Node, Base, &Size)) || Size < 0x420)
    return EFI_UNSUPPORTED;
  if ((*Group == 1 && *Base == 0xfffff4a000ULL) ||
      (*Group == 2 && *Base == 0xffe7f3c000ULL) ||
      (*Group == 3 && *Base == 0xffec007000ULL))
    return EnableClock (Fdt, Node, FALSE);
  return EFI_UNSUPPORTED;
}

STATIC INTN
GpioMux (UINT32 Group, UINTN Pin)
{
  if (Group == 1 && Pin >= 9 && Pin <= 46)
    return Pin >= 20 && Pin <= 24 ? 0 : 3;
  if (Group == 2 && Pin < 63)
    return Pin < 18 || (Pin >= 49 && Pin <= 57) ? 3 : 0;
  if (Group == 3 && Pin < 55)
    return Pin == 13 || (Pin >= 18 && Pin <= 25) ||
                   (Pin >= 30 && Pin <= 35)
               ? 0
               : 3;
  return -1;
}

STATIC INTN
PinNumber (CONST CHAR8 *Name, UINT32 Group)
{
  STATIC CONST struct
  {
    CONST CHAR8 *Name;
    UINT8 Group, Pin;
  } Names[] = {{"QSPI0_SCLK", 3, 2},    {"QSPI0_CSN0", 3, 3},
               {"QSPI0_CSN1", 3, 4},    {"QSPI0_D0_MOSI", 3, 5},
               {"QSPI0_D1_MISO", 3, 6}, {"QSPI0_D2_WP", 3, 7},
               {"I2C0_SCL", 2, 6},      {"I2C0_SDA", 2, 7},
               {"I2C1_SCL", 2, 8},      {"I2C1_SDA", 2, 9},
               {"I2C2_SCL", 3, 9},      {"I2C2_SDA", 3, 10},
               {"I2C3_SCL", 3, 11},     {"I2C3_SDA", 3, 12},
               {"HDMI_SCL", 3, 36},     {"HDMI_SDA", 3, 37},
               {"GMAC0_TX_CLK", 3, 39}, {"GMAC0_RX_CLK", 3, 40},
               {"GMAC0_TXEN", 3, 41},   {"GMAC0_TXD0", 3, 42},
               {"GMAC0_TXD1", 3, 43},   {"GMAC0_TXD2", 3, 44},
               {"GMAC0_TXD3", 3, 45},   {"GMAC0_RXDV", 3, 46},
               {"GMAC0_RXD0", 3, 47},   {"GMAC0_RXD1", 3, 48},
               {"GMAC0_RXD2", 3, 49},   {"GMAC0_RXD3", 3, 50},
               {"GMAC0_MDC", 3, 51},    {"GMAC0_MDIO", 3, 52},
               {"GMAC0_COL", 3, 53},    {"GMAC0_CRS", 3, 54},
               {"CPU_JTG_TDI", 1, 13}};
  UINTN Pin, Bank;
  CHAR8 *End;
  for (UINTN I = 0; I < ARRAY_SIZE (Names); I++)
    if (Group == Names[I].Group && !AsciiStrCmp (Name, Names[I].Name))
      return Names[I].Pin;
  if (AsciiStrLen (Name) < 7 || AsciiStrnCmp (Name, "GPIO", 4) ||
      Name[4] < '0' || Name[4] > '3' || Name[5] != '_' ||
      EFI_ERROR (AsciiStrDecimalToUintnS (Name + 6, &End, &Pin)) || *End ||
      Pin > 31)
    return -1;
  Bank = Name[4] - '0';
  if (Group == 2 &&
      ((Bank == 0 && Pin >= 18) ||
       (Bank == 1 && (Pin <= 16 || (Pin >= 21 && Pin <= 30)))))
    return Bank * 32 + Pin;
  if (Group == 3 &&
      ((Bank == 2 &&
        (Pin == 13 || (Pin >= 18 && Pin <= 25) || Pin >= 30)) ||
       (Bank == 3 && Pin <= 3)))
    return (Bank - 2) * 32 + Pin;
  return -1;
}

STATIC INTN
PinMux (UINT32 Group, UINTN Pin, CONST CHAR8 *Function)
{
  if (!AsciiStrCmp (Function, "gpio"))
    return GpioMux (Group, Pin);
  if (!AsciiStrCmp (Function, "pwm") && Group == 3 &&
      ((Pin >= 2 && Pin <= 7) || (Pin >= 34 && Pin <= 37) || Pin == 53 ||
       Pin == 54))
    return 1;
  if (!AsciiStrCmp (Function, "i2c") &&
      ((Group == 2 && Pin >= 6 && Pin <= 9) ||
       (Group == 3 && Pin >= 9 && Pin <= 12)))
    return 0;
  if (!AsciiStrCmp (Function, "gmac0") && Group == 3 && Pin >= 39 &&
      Pin <= 54)
    return 0;
  if (!AsciiStrCmp (Function, "gmac1") && Group == 3)
  {
    if ((Pin >= 18 && Pin <= 25) || (Pin >= 30 && Pin <= 33))
      return 1;
    if (Pin == 51 || Pin == 52)
      return 2;
  }
  return -1;
}

STATIC EFI_STATUS
PinConfig (CONST VOID *Fdt, INT32 Node, UINT32 *Mask, UINT32 *Config)
{
  STATIC CONST UINT8 Drive[] = {1,  2,  3,  5,  7,  8,  10, 12,
                                13, 15, 16, 18, 20, 21, 23, 25};
  STATIC CONST struct
  {
    CONST CHAR8 *Enable, *Disable;
    UINT32 Bit;
  } Bits[] = {{"input-enable", "input-disable", BIT9},
              {"input-schmitt-enable", "input-schmitt-disable", BIT7}};
  UINT32 Value;
  EFI_STATUS Status;
  *Mask = *Config = 0;
  Status = ReadCell (Fdt, Node, "drive-strength", &Value);
  if (Status != EFI_NOT_FOUND)
  {
    if (EFI_ERROR (Status) || Value > 25)
      return EFI_UNSUPPORTED;
    UINTN I = 0;
    while (I + 1 < ARRAY_SIZE (Drive) && Value > Drive[I])
      I++;
    *Mask |= 15;
    *Config |= I;
  }
  for (UINTN I = 0; I < ARRAY_SIZE (Bits); I++)
  {
    BOOLEAN Enable = Has (Fdt, Node, Bits[I].Enable);
    BOOLEAN Disable = Has (Fdt, Node, Bits[I].Disable);
    if (Enable && Disable)
      return EFI_COMPROMISED_DATA;
    if (Enable || Disable)
      *Mask |= Bits[I].Bit;
    if (Enable)
      *Config |= Bits[I].Bit;
  }
  Status = ReadCell (Fdt, Node, "slew-rate", &Value);
  if (Status != EFI_NOT_FOUND)
  {
    if (EFI_ERROR (Status) || Value > 1)
      return EFI_UNSUPPORTED;
    *Mask |= BIT8;
    *Config |= Value ? BIT8 : 0;
  }
  BOOLEAN Up = Has (Fdt, Node, "bias-pull-up");
  BOOLEAN Down = Has (Fdt, Node, "bias-pull-down");
  BOOLEAN Off = Has (Fdt, Node, "bias-disable");
  if ((UINTN)Up + Down + Off > 1)
    return EFI_COMPROMISED_DATA;
  if (Up || Down || Off)
    *Mask |= BIT4 | BIT5 | BIT6;
  if (Up)
  {
    INT32 Bytes;
    CONST VOID *P = FdtGetProp (Fdt, Node, "bias-pull-up", &Bytes);
    if (Bytes != 0 && Bytes != 4)
      return EFI_COMPROMISED_DATA;
    Value = Bytes ? Cell (P) : 48000;
    if (Value != 1 && Value != 2100 && Value != 48000)
      return EFI_UNSUPPORTED;
    *Config |= Value == 2100 ? BIT6 : BIT4 | BIT5;
  }
  if (Down)
  {
    INT32 Bytes;
    CONST VOID *P = FdtGetProp (Fdt, Node, "bias-pull-down", &Bytes);
    if (Bytes != 0 && Bytes != 4)
      return EFI_COMPROMISED_DATA;
    if (Bytes && !Cell (P))
      return EFI_UNSUPPORTED;
    *Config |= BIT4;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
PinState (CONST VOID *Fdt, INT32 Node, BOOLEAN Apply)
{
  CONST UINT32 *Groups;
  CONST CHAR8 *Names, *Function;
  INT32 Index, Bytes, GroupBytes, Controller, Child, Length;
  UINTN Base, NameSize, Shift;
  UINT32 Group, Mask, Config;
  EFI_STATUS Status;
  Index = FdtStringListSearch (Fdt, Node, "pinctrl-names", "default");
  if (Index < 0)
    return Has (Fdt, Node, "pinctrl-0") ? EFI_UNSUPPORTED : EFI_SUCCESS;
  // Current TH1520 descriptions put the default state first.
  if (Index != 0)
    return EFI_UNSUPPORTED;
  Groups = FdtGetProp (Fdt, Node, "pinctrl-0", &GroupBytes);
  if (!Groups || GroupBytes <= 0 || GroupBytes % 4)
    return EFI_COMPROMISED_DATA;
  for (INT32 I = 0; I < GroupBytes / 4; I++)
  {
    INT32 Pins = FdtNodeOffsetByPhandle (Fdt, Cell (&Groups[I]));
    Controller = FdtParentOffset (Fdt, Pins);
    if (!Th1520FdtEnabled (Fdt, Pins) ||
        EFI_ERROR (PadController (Fdt, Controller, &Base, &Group)))
      return EFI_UNSUPPORTED;
    if (Apply && EFI_ERROR (EnableClock (Fdt, Controller, TRUE)))
      return EFI_DEVICE_ERROR;
    FdtForEachSubnode (Child, Fdt, Pins)
    {
      if (!Th1520FdtEnabled (Fdt, Child))
        continue;
      Status = PinConfig (Fdt, Child, &Mask, &Config);
      if (EFI_ERROR (Status))
        return Status;
      Function = FdtGetProp (Fdt, Child, "function", &Length);
      if (Function && (Length <= 0 || AsciiStrnLenS (Function, Length) !=
                                          (UINTN)Length - 1))
        return EFI_COMPROMISED_DATA;
      Names = FdtGetProp (Fdt, Child, "pins", &Bytes);
      if (!Names || Bytes <= 0)
        return EFI_COMPROMISED_DATA;
      for (INT32 Offset = 0; Offset < Bytes; Offset += NameSize + 1)
      {
        NameSize = AsciiStrnLenS (Names + Offset, Bytes - Offset);
        if (!NameSize || NameSize == (UINTN)(Bytes - Offset))
          return EFI_COMPROMISED_DATA;
        INTN Pin = PinNumber (Names + Offset, Group);
        INTN Mux = Function ? PinMux (Group, Pin, Function) : -1;
        if (Pin < 0 || (Function && Mux < 0))
          return EFI_UNSUPPORTED;
        if (!Apply)
          continue;
        Shift = 16 * (Pin % 2);
        MmioAndThenOr32 (Base + 4 * (Pin / 2), ~(Mask << Shift),
                         Config << Shift);
        if (Function)
        {
          Shift = 4 * (Pin % 8);
          MmioAndThenOr32 (Base + 0x400 + 4 * (Pin / 8), ~(15U << Shift),
                           (UINT32)Mux << Shift);
        }
      }
    }
  }
  return EFI_SUCCESS;
}

EFI_STATUS
Th1520FdtPinctrl (CONST VOID *Fdt, INT32 Node)
{
  EFI_STATUS Status = PinState (Fdt, Node, FALSE);
  return EFI_ERROR (Status) ? Status : PinState (Fdt, Node, TRUE);
}
