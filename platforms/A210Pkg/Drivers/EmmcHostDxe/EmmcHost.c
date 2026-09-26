// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <A210.h>
#include <Library/BaseLib.h>
#include <Library/A210ClockLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/NonDiscoverableDeviceRegistrationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/FdtClient.h>
#include <Protocol/SdMmcOverride.h>
#include "Emmc.h"

STATIC EFI_HANDLE mController;
STATIC UINTN mBase;
STATIC UINT32 mClockHz;
STATIC UINT32 mBusWidth;
STATIC UINT32 mMaxFrequency;

STATIC VOID
ConfigurePhy (VOID)
{
  UINT16 Pad = PHY_PAD_SLEW | PHY_PAD_PULL_UP | PHY_PAD_RX;

  MmioWrite32 (mBase + PHY_CONFIG, PHY_RESET_N | PHY_PAD_DRIVE);
  MmioWrite8 (mBase + PHY_CLOCK_DELAY_CONFIG, PHY_DELAY_UPDATE);
  MmioWrite8 (mBase + PHY_CLOCK_DELAY, PHY_SDR_DELAY_TAPS);
  MmioWrite8 (mBase + PHY_DLL_CONFIG2, PHY_DLL_START);
  MmioWrite8 (mBase + PHY_CLOCK_DELAY_CONFIG, 0);
  MmioWrite16 (mBase + PHY_CMD_PAD, Pad);
  MmioWrite16 (mBase + PHY_DATA_PAD, Pad);
  MmioWrite16 (mBase + PHY_RESET_PAD, Pad);
  MmioWrite16 (mBase + PHY_CLK_PAD, PHY_PAD_SLEW);
  MmioWrite16 (mBase + PHY_STROBE_PAD,
               PHY_PAD_SLEW | PHY_PAD_PULL_DOWN | PHY_PAD_RX);
  MmioWrite8 (mBase + PHY_DLL_DELAY_CONFIG, 0);
  MmioWrite8 (mBase + PHY_DLL_CONTROL, PHY_DLL_ENABLE);
  MmioWrite8 (mBase + PHY_DLL_CONFIG1, PHY_DLL_WAIT);
  MmioOr8 (mBase + DWC_EMMC_CONTROL, DWC_EMMC_CARD);
}

STATIC EFI_STATUS EFIAPI
Capability (EFI_HANDLE Controller, UINT8 Slot, VOID *Caps,
            UINT32 *BaseClock)
{
  UINT64 Value;

  if (Controller != mController || Slot != 0)
  {
    return EFI_NOT_FOUND;
  }
  if (Caps == NULL || BaseClock == NULL)
  {
    return EFI_INVALID_PARAMETER;
  }
  // The standard protocol expresses its reference clock in whole MHz.
  // Round up so divider selection never exceeds the requested frequency.
  *BaseClock = (mClockHz + 999999) / 1000000;
  Value = ReadUnaligned64 (Caps) & SDHCI_CAP_TIMEOUT_MASK;
  Value |= LShiftU64 (*BaseClock, SDHCI_CAP_CLOCK_SHIFT)
           | SDHCI_CAP_HIGH_SPEED | SDHCI_CAP_3V3 | SDHCI_CAP_EMBEDDED;
  if (mBusWidth == 8)
  {
    Value |= SDHCI_CAP_8BIT;
  }
  // Use PIO until DMA routing and coherency have been validated.
  WriteUnaligned64 (Caps, Value);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
EnableCardClock (VOID)
{
  UINT16 Clock = MmioRead16 (mBase + SDHCI_CLOCK_CONTROL);

  Clock &= ~SDHCI_CLOCK_CARD_ENABLE;
  MmioWrite16 (mBase + SDHCI_CLOCK_CONTROL, Clock);
  if (MmioRead16 (mBase + SDHCI_HOST_CONTROL2) & SDHCI_HOST_VERSION4)
  {
    // SDHCI v4.10 requires PLL_EN before enabling the card clock.
    Clock = (Clock & ~SDHCI_CLOCK_STABLE) | SDHCI_CLOCK_PLL_ENABLE;
    MmioWrite16 (mBase + SDHCI_CLOCK_CONTROL, Clock);
    for (UINTN Wait = 0;; Wait += 10)
    {
      if (MmioRead16 (mBase + SDHCI_CLOCK_CONTROL) & SDHCI_CLOCK_STABLE)
      {
        break;
      }
      if (Wait >= SDHCI_CLOCK_TIMEOUT_US)
      {
        return EFI_TIMEOUT;
      }
      MicroSecondDelay (10);
    }
  }
  MmioOr16 (mBase + SDHCI_CLOCK_CONTROL, SDHCI_CLOCK_CARD_ENABLE);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
NotifyPhase (EFI_HANDLE Controller, UINT8 Slot,
             EDKII_SD_MMC_PHASE_TYPE Phase, VOID *Data)
{
  EDKII_SD_MMC_OPERATING_PARAMETERS *Parameters;

  if (Controller != mController || Slot != 0)
  {
    return EFI_NOT_FOUND;
  }
  switch (Phase)
  {
  case EdkiiSdMmcResetPost:
    ConfigurePhy ();
    break;
  case EdkiiSdMmcInitHostPost:
    // Card power switching clears SDCLK on this controller.
    MmioOr16 (mBase + SDHCI_HOST_CONTROL2, SDHCI_SIGNAL_1V8);
    return EnableCardClock ();
  case EdkiiSdMmcSwitchClockFreqPost:
    // The generic clock update also clears PLL_EN.
    return EnableCardClock ();
  case EdkiiSdMmcUhsSignaling:
    // VCC is 3.3 V; Melon Pi VCCQ is fixed at 1.8 V.
    MmioOr16 (mBase + SDHCI_HOST_CONTROL2, SDHCI_SIGNAL_1V8);
    break;
  case EdkiiSdMmcGetOperatingParam:
    if (Data == NULL)
    {
      return EFI_INVALID_PARAMETER;
    }
    Parameters = Data;
    Parameters->BusWidth = (UINT8)mBusWidth;
    Parameters->ClockFreq = mMaxFrequency / 1000000;
    Parameters->DriverStrength.Emmc = EmmcDriverStrengthType0;
    break;
  default:
    break;
  }
  return EFI_SUCCESS;
}

STATIC EDKII_SD_MMC_OVERRIDE mOverride
    = { EDKII_SD_MMC_OVERRIDE_PROTOCOL_VERSION, Capability, NotifyPhase };

STATIC EFI_STATUS
Property32 (FDT_CLIENT_PROTOCOL *Fdt, INT32 Node, CONST CHAR8 *Name,
            UINT32 *Value)
{
  CONST VOID *Data;
  UINT32 Size;
  EFI_STATUS Status;

  Status = Fdt->GetNodeProperty (Fdt, Node, Name, &Data, &Size);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  if (Size != sizeof (UINT32))
  {
    return EFI_COMPROMISED_DATA;
  }
  *Value = SwapBytes32 (ReadUnaligned32 (Data));
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI
EmmcHostInitialize (EFI_HANDLE Image, EFI_SYSTEM_TABLE *SystemTable)
{
  FDT_CLIENT_PROTOCOL *Fdt;
  EFI_STATUS Status;
  CONST VOID *Data;
  UINT32 Size;
  INT32 Node;
  EFI_HANDLE OverrideHandle = NULL;

  Status
      = gBS->LocateProtocol (&gFdtClientProtocolGuid, NULL, (VOID **)&Fdt);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = Fdt->FindCompatibleNode (Fdt, "zhihe,a210-dwcmshc", &Node);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = Fdt->GetNodeProperty (Fdt, Node, "status", &Data, &Size);
  if (!EFI_ERROR (Status)
      && !((Size == 5 && CompareMem (Data, "okay", 5) == 0)
           || (Size == 3 && CompareMem (Data, "ok", 3) == 0)))
  {
    return EFI_UNSUPPORTED;
  }
  Status = Fdt->GetNodeProperty (Fdt, Node, "reg", &Data, &Size);
  if (EFI_ERROR (Status) || Size < 16)
  {
    return EFI_UNSUPPORTED;
  }
  mBase = SwapBytes64 (ReadUnaligned64 (Data));
  if (mBase != A210_EMMC_BASE
      || SwapBytes64 (ReadUnaligned64 ((CONST UINT8 *)Data + 8))
             < A210_EMMC_SIZE)
  {
    return EFI_UNSUPPORTED;
  }
  Status = Fdt->GetNodeProperty (Fdt, Node, "non-removable", &Data, &Size);
  if (EFI_ERROR (Status))
  {
    return EFI_UNSUPPORTED;
  }
  Status = Property32 (Fdt, Node, "bus-width", &mBusWidth);
  if (EFI_ERROR (Status)
      || (mBusWidth != 1 && mBusWidth != 4 && mBusWidth != 8))
  {
    return EFI_UNSUPPORTED;
  }
  mMaxFrequency = 52000000;
  Status = Property32 (Fdt, Node, "max-frequency", &mMaxFrequency);
  if ((EFI_ERROR (Status) && Status != EFI_NOT_FOUND)
      || mMaxFrequency < 1000000)
  {
    return EFI_UNSUPPORTED;
  }
  mMaxFrequency = MIN (mMaxFrequency, 52000000);
  mClockHz = A210EmmcClock ();
  if (mClockHz == 0)
  {
    return EFI_DEVICE_ERROR;
  }
  MmioOr32 (PERI3_CLOCK_ENABLE, PERI3_EMMC_CLOCKS);
  MmioOr32 (PERI3_RESET_N, PERI3_EMMC_RESETS);
  Status = gBS->InstallProtocolInterface (
      &OverrideHandle, &gEdkiiSdMmcOverrideProtocolGuid,
      EFI_NATIVE_INTERFACE, &mOverride);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = RegisterNonDiscoverableMmioDevice (
      NonDiscoverableDeviceTypeSdhci,
      NonDiscoverableDeviceDmaTypeNonCoherent, NULL, &mController, 1,
      mBase, (UINTN)A210_EMMC_SIZE);
  if (EFI_ERROR (Status))
  {
    gBS->UninstallProtocolInterface (
        OverrideHandle, &gEdkiiSdMmcOverrideProtocolGuid, &mOverride);
    return Status;
  }
  DEBUG (
      (DEBUG_INFO, "A210 eMMC: %u Hz, %u-bit PIO\n", mClockHz, mBusWidth));
  return EFI_SUCCESS;
}
