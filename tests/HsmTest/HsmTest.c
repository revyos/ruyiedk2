// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseRiscVSbiLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/RiscVBootProtocol.h>

#define HSM 0x48534d
#define HARTS 4
#define ROUNDS 16
#define RESET_REG 0xffef014004ULL

typedef struct
{
  volatile UINTN Expected, Observed, HartId, Entered, Release, StopError;
  UINTN Padding[2];
} HART_DATA;

VOID HsmSecondary (VOID);

STATIC BOOLEAN
Stopped (UINTN Hart)
{
  SBI_RET Ret = SbiCall (HSM, 2, 1, Hart);
  return Ret.Error == 0 && Ret.Value == 1;
}

STATIC BOOLEAN
WaitStopped (UINTN Hart)
{
  UINTN Retry;
  for (Retry = 0; Retry < 20000; Retry++)
  {
    if (Stopped (Hart))
    {
      return TRUE;
    }
    MicroSecondDelay (100);
  }
  return FALSE;
}

EFI_STATUS EFIAPI
HsmTestMain (EFI_HANDLE Image, EFI_SYSTEM_TABLE *SystemTable)
{
  RISCV_EFI_BOOT_PROTOCOL *Boot;
  EFI_STATUS Status;
  SBI_RET Ret;
  HART_DATA *Data;
  UINTN BootHart, Hart, Round, Retry;
  UINT32 InitialReset;

  Status = gBS->LocateProtocol (&gRiscVEfiBootProtocolGuid, NULL, (VOID **)&Boot);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = Boot->GetBootHartId (Boot, &BootHart);
  if (EFI_ERROR (Status) || BootHart >= HARTS)
  {
    return EFI_UNSUPPORTED;
  }
  gBS->SetWatchdogTimer (0, 0, 0, NULL);
  Data = AllocatePages (1);
  if (!Data)
  {
    return EFI_OUT_OF_RESOURCES;
  }
  ZeroMem (Data, EFI_PAGE_SIZE);
  InitialReset = MmioRead32 (RESET_REG) & 0x1f;
  Print (L"HSM test: boot hart %u, initial reset mask 0x%x\r\n", BootHart, InitialReset);
  if (InitialReset != 3 && InitialReset != 0x1f)
  {
    goto Failed;
  }
  if (InitialReset == 3 && BootHart != 0)
  {
    goto Failed;
  }

  Ret = SbiCall (HSM, 2, 1, (UINTN)HARTS);
  if ((INTN)Ret.Error != SBI_ERR_INVALID_PARAM)
  {
    goto Failed;
  }
  Ret = SbiCall (HSM, 0, 3, (UINTN)HARTS, (UINTN)HsmSecondary, (UINTN)Data);
  if ((INTN)Ret.Error != SBI_ERR_INVALID_PARAM)
  {
    goto Failed;
  }

  for (Round = 0; Round < ROUNDS; Round++)
  {
    for (Hart = 0; Hart < HARTS; Hart++)
    {
      if (Hart == BootHart)
      {
        continue;
      }
      if (!Stopped (Hart))
      {
        goto Failed;
      }
      Data[Hart].Expected = 0x48534d00 | (Round << 4) | Hart;
      Data[Hart].Observed = Data[Hart].Entered = Data[Hart].Release = 0;
      Data[Hart].StopError = MAX_UINTN;
      MemoryFence ();
      Ret = SbiCall (HSM, 0, 3, Hart, (UINTN)HsmSecondary, (UINTN)&Data[Hart]);
      if (Ret.Error)
      {
        Print (L"hart %u start: %ld\r\n", Hart, Ret.Error);
        goto Failed;
      }
    }
    for (Hart = 0; Hart < HARTS; Hart++)
    {
      if (Hart == BootHart)
      {
        continue;
      }
      for (Retry = 0; Retry < 20000 && !Data[Hart].Entered; Retry++)
      {
        MicroSecondDelay (100);
      }
      MemoryFence ();
      if (!Data[Hart].Entered || Data[Hart].HartId != Hart ||
          Data[Hart].Observed != Data[Hart].Expected)
      {
        goto Failed;
      }
      Ret = SbiCall (HSM, 2, 1, Hart);
      if (Ret.Error || Ret.Value != 0)
      {
        goto Failed;
      }
      Ret = SbiCall (HSM, 0, 3, Hart, (UINTN)HsmSecondary, (UINTN)&Data[Hart]);
      if ((INTN)Ret.Error != SBI_ERR_ALREADY_AVAILABLE)
      {
        goto Failed;
      }
    }
    for (Hart = 0; Hart < HARTS; Hart++)
    {
      if (Hart != BootHart)
      {
        Data[Hart].Release = 1;
      }
    }
    MemoryFence ();
    for (Hart = 0; Hart < HARTS; Hart++)
    {
      if (Hart != BootHart && !WaitStopped (Hart))
      {
        goto Failed;
      }
    }
    if ((MmioRead32 (RESET_REG) & 0x1f) != 0x1f)
    {
      goto Failed;
    }
    Print (L"PASS round %u: all secondary harts started, shared data verified, stopped\r\n",
           Round + 1);
  }
  Print (L"PASS HSM: %u cycles per secondary, %s first start\r\n", ROUNDS,
         InitialReset == 3 ? L"hardware reset release" : L"legacy SPL/IPI");
  FreePages (Data, 1);
  return EFI_SUCCESS;
Failed:
  Print (L"FAIL HSM: reset mask 0x%x\r\n", MmioRead32 (RESET_REG));
  for (Hart = 0; Hart < HARTS; Hart++)
  {
    if (Hart != BootHart)
    {
      Data[Hart].Release = 1;
    }
  }
  MemoryFence ();
  for (Hart = 0; Hart < HARTS; Hart++)
  {
    if (Hart != BootHart && !WaitStopped (Hart))
    {
      Print (L"Hart %u did not stop; retaining test code and buffers. Reset required.\r\n", Hart);
      CpuDeadLoop ();
    }
  }
  FreePages (Data, 1);
  return EFI_DEVICE_ERROR;
}
