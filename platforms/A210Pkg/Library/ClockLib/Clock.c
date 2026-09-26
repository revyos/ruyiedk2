// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Base.h>
#include <A210.h>
#include <Library/A210ClockLib.h>
#include <Library/BaseLib.h>
#include <Library/IoLib.h>

#define OSCILLATOR_HZ 24000000
#define PLL_CONFIG0 0x00
#define PLL_CONFIG1 0x04
#define PLL_STATUS 0x10
#define PLL_REFDIV_MASK 0x3f
#define PLL_FBDIV_SHIFT 8
#define PLL_FBDIV_MASK 0xfff
#define PLL_FRAC_BITS 24
#define PLL_FRAC_MASK 0xffffff
#define PLL_INTEGER_MODE BIT24
#define PLL_RESET BIT30
#define PLL_BYPASS BIT31
#define PLL_LOCK BIT0
#define EMMC_DIVIDER (A210_TOP_CRG_BASE + 0x28)
#define EMMC_MUX (A210_TOP_CRG_BASE + 0x2c)
#define EMMC_VIDEO_PLL BIT20
#define EMMC_DIV_MASK 0xf
#define EMMC_FIXED_DIVIDER 4
#define AUDIO0_PLL (A210_PLL_BASE + 0x00)
#define VIDEO_PLL (A210_PLL_BASE + 0x140)
#define DPU1_PLL (A210_PLL_BASE + 0x100)
#define C908_PLL (A210_CPU_CLOCK_BASE + 0x1400)
#define C920_PLL (A210_CPU_CLOCK_BASE + 0x1440)
#define C908_CCU (A210_CPU_CLOCK_BASE + 0x0a00)
#define C920_CCU (A210_CPU_CLOCK_BASE + 0x0c00)
#define CPU_CLOCK_MUX (A210_CPU_CLOCK_BASE + 0x1600)
#define CPU_CLOCK_FROM_TOP BIT0
#define CORES_PER_CLUSTER 4
#define CCU_CONTROL 0x04
#define CCU_SOFTWARE_MODE BIT0
#define CCU_LOW_POWER 0x10
#define CCU_HIGH_SPEED BIT0
#define CCU_CLOCK_ENABLED BIT1
#define CCU_RATIO_LOW 0x18
#define CCU_RATIO_NORMAL 0x1c
#define CCU_RATIO_MASK 0xffff

STATIC UINT64
PllVco (UINTN Base)
{
  UINT32 Config0 = MmioRead32 (Base + PLL_CONFIG0);
  UINT32 Config1 = MmioRead32 (Base + PLL_CONFIG1);
  UINT32 RefDiv = Config0 & PLL_REFDIV_MASK;
  UINT64 Feedback;

  if (Config1 & PLL_BYPASS)
    return OSCILLATOR_HZ;
  if (RefDiv == 0 || (Config1 & PLL_RESET)
      || !(MmioRead32 (Base + PLL_STATUS) & PLL_LOCK))
    return 0;
  Feedback = LShiftU64 ((Config0 >> PLL_FBDIV_SHIFT) & PLL_FBDIV_MASK,
                        PLL_FRAC_BITS);
  if (!(Config1 & PLL_INTEGER_MODE))
    Feedback += Config1 & PLL_FRAC_MASK;
  return DivU64x32 (
      RShiftU64 (MultU64x32 (Feedback, OSCILLATOR_HZ), PLL_FRAC_BITS),
      RefDiv);
}

UINT32
A210EmmcClock (VOID)
{
  UINT32 Divider = MmioRead32 (EMMC_DIVIDER) & EMMC_DIV_MASK;
  UINTN Pll
      = (MmioRead32 (EMMC_MUX) & EMMC_VIDEO_PLL) ? VIDEO_PLL : AUDIO0_PLL;
  if (Divider == 0)
    return 0;
  return (UINT32)DivU64x32 (PllVco (Pll), Divider * EMMC_FIXED_DIVIDER);
}

UINT64
A210CpuClock (UINTN HartId)
{
  UINTN Cluster = HartId / CORES_PER_CLUSTER;
  UINTN Ccu, Pll, Ratio = CCU_RATIO_NORMAL;

  if (Cluster > 1)
    return 0;
  if (MmioRead32 (CPU_CLOCK_MUX + Cluster * sizeof (UINT32))
      & CPU_CLOCK_FROM_TOP)
    return 0;
  Ccu = Cluster ? C920_CCU : C908_CCU;
  Pll = Cluster ? C920_PLL : C908_PLL;
  if (MmioRead32 (Ccu + CCU_CONTROL) & CCU_SOFTWARE_MODE)
  {
    UINT32 Control = MmioRead32 (Ccu + CCU_LOW_POWER);
    if (!(Control & CCU_CLOCK_ENABLED))
      return 0;
    if (!(Control & CCU_HIGH_SPEED))
      Ratio = CCU_RATIO_LOW;
  }
  return DivU64x32 (PllVco (Pll),
                    (MmioRead32 (Ccu + Ratio) & CCU_RATIO_MASK) + 1);
}

UINT32
A210PeripheralClock (VOID)
{
  UINT32 Divider = (MmioRead32 (A210_TOP_CRG_BASE) >> 8) & 0xff;
  if (Divider == 0)
    return 0;
  return (UINT32)DivU64x32 (PllVco (VIDEO_PLL), Divider * 4);
}

UINT64
A210DisplayClock (VOID)
{
  UINT32 Config = MmioRead32 (DPU1_PLL + PLL_CONFIG0);
  UINT32 Post1 = (Config >> 20) & 7;
  UINT32 Post2 = (Config >> 24) & 7;
  if (!Post1 || !Post2)
    return 0;
  return DivU64x32 (PllVco (DPU1_PLL), Post1 * Post2);
}
