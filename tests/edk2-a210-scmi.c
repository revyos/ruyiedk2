// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libfdt.h>
#include "../platforms/A210Pkg/Drivers/ScmiDxe/Scmi.c"

static UINT32 memory[64];
static unsigned int doorbells, mode, delays;
static EFI_TPL current_tpl = TPL_APPLICATION;
static EFI_STATUS nested_status;

static EFI_TPL EFIAPI
Raise (EFI_TPL NewTpl)
{
  EFI_TPL old = current_tpl;
  assert (NewTpl >= old);
  current_tpl = NewTpl;
  return old;
}
static VOID EFIAPI
Restore (EFI_TPL OldTpl)
{
  assert (current_tpl == TPL_NOTIFY);
  current_tpl = OldTpl;
}
static EFI_BOOT_SERVICES services = { .RaiseTPL = Raise, .RestoreTPL = Restore };
EFI_BOOT_SERVICES *gBS = &services;

INT32 EFIAPI
FdtStringListContains (CONST CHAR8 *List, INT32 Length, CONST CHAR8 *String)
{
  return fdt_stringlist_contains (List, Length, String);
}
UINT32 EFIAPI
MmioRead32 (UINTN Address)
{
  assert (Address >= mShmem && Address < mShmem + sizeof (memory));
  assert (!(Address & 3));
  return memory[(Address - mShmem) / 4];
}
UINT32 EFIAPI
MmioWrite32 (UINTN Address, UINT32 Value)
{
  if (Address == mDoorbell)
  {
    UINT32 nested[2], length = sizeof (nested);
    assert (current_tpl == TPL_NOTIFY && Value == MBOX_NOTIFY);
    assert (memory[SMT_STATUS / 4] == 0);
    doorbells++;
    if (mode == 1) // No completion: the channel must become unusable.
      return Value;
    if (mode == 6)
      nested_status = Execute (&mProtocol, 0x10, 0, nested, 0, &length);
    memory[SMT_LENGTH / 4] = mode == 2 ? 4 : (mode == 3 ? 236 : 12);
    if (mode == 4)
      memory[SMT_HEADER / 4] ^= 1U << 18;
    memory[SMT_PAYLOAD / 4] = mode == 5 ? (UINT32)-3 : 0;
    memory[SMT_PAYLOAD / 4 + 1] = 0x20000;
    memory[SMT_STATUS / 4] = SMT_FREE;
    return Value;
  }
  assert (Address >= mShmem && Address < mShmem + sizeof (memory));
  assert (!(Address & 3));
  memory[(Address - mShmem) / 4] = Value;
  return Value;
}
VOID EFIAPI
MemoryFence (VOID)
{
}
UINTN EFIAPI
MicroSecondDelay (UINTN Delay)
{
  delays += Delay;
  return Delay;
}
static void
reset (void)
{
  memset (memory, 0, sizeof (memory));
  memory[SMT_STATUS / 4] = SMT_FREE;
  mFailed = mBusy = mExited = FALSE;
  mode = delays = doorbells = 0;
  current_tpl = TPL_APPLICATION;
}
static void
check_dtb (const char *path)
{
  FILE *file = fopen (path, "rb");
  long size;
  void *blob;
  int node;
  fdt32_t cells[2] = { 0, cpu_to_fdt32 (0x104) };
  assert (file && !fseek (file, 0, SEEK_END));
  size = ftell (file);
  assert (size > 0 && !fseek (file, 0, SEEK_SET));
  blob = malloc (size + 4096);
  assert (blob && fread (blob, 1, size, file) == (size_t)size);
  fclose (file);
  assert (!fdt_open_into (blob, blob, size + 4096));
  assert (LocateChannel (blob) == EFI_SUCCESS);
  assert (mShmem == 0x30907c00 && mDoorbell == 0x00311010);
  node = fdt_node_offset_by_compatible (blob, -1, "arm,scmi-shmem");
  assert (node >= 0 && !fdt_setprop (blob, node, "reg", cells, sizeof (cells)));
  // The SRAM child must not extend beyond the parent's translated range.
  assert (LocateChannel (blob) == EFI_UNSUPPORTED);
  free (blob);
}
int
main (int argc, char **argv)
{
  UINT32 buffer[3] = { 0, 0, 0xfeedface }, size;
  assert (argc > 1);
  for (int i = 1; i < argc; i++)
    check_dtb (argv[i]);
  reset ();
  size = 8;
  assert (Execute (&mProtocol, 0x10, 0, buffer, 0, &size) == EFI_SUCCESS);
  assert (size == 8 && !buffer[0] && buffer[1] == 0x20000);
  assert (buffer[2] == 0xfeedface && doorbells == 1 && !delays);
  assert (current_tpl == TPL_APPLICATION);
  size = 4;
  assert (Execute (&mProtocol, 0x10, 0, buffer, 0, &size) == EFI_BUFFER_TOO_SMALL);
  assert (size == 8 && !mFailed);
  assert (Execute (&mProtocol, 0x10, 0, buffer, 229, &size) == EFI_INVALID_PARAMETER);
  for (unsigned int error = 1; error <= 4; error++)
  {
    reset ();
    mode = error;
    size = 8;
    assert (Execute (&mProtocol, 0x10, 0, buffer, 0, &size)
            == (error == 1 ? EFI_TIMEOUT : EFI_PROTOCOL_ERROR));
    assert (mFailed && !mBusy && current_tpl == TPL_APPLICATION);
    assert (Execute (&mProtocol, 0x10, 0, buffer, 0, &size) == EFI_DEVICE_ERROR);
    assert (doorbells == 1);
  }
  reset ();
  mode = 5;
  size = 8;
  assert (Execute (&mProtocol, 0x10, 0, buffer, 0, &size) == EFI_SUCCESS);
  assert ((INT32)buffer[0] == -3 && !mFailed);
  reset ();
  mode = 6;
  assert (Execute (&mProtocol, 0x10, 0, buffer, 0, &size) == EFI_SUCCESS);
  assert (nested_status == EFI_NOT_READY && doorbells == 1);
  ExitBootServices (NULL, NULL);
  assert (Execute (&mProtocol, 0x10, 0, buffer, 0, &size) == EFI_DEVICE_ERROR);
  reset ();
  memory[SMT_STATUS / 4] = 0;
  assert (Execute (&mProtocol, 0x10, 0, buffer, 0, &size) == EFI_TIMEOUT);
  assert (doorbells == 0 && mFailed && delays == TIMEOUT_US);
  puts ("A210 SCMI: DT translation, completion, bounds, errors and handoff passed");
  return 0;
}
