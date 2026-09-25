// SPDX-License-Identifier: BSD-2-Clause-Patent
// Exercise the firmware's flash validator on the host, with real libfdt/SHA256.
#include <Uefi.h>
#include <Library/BaseCryptLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/FdtLib.h>
#include <libfdt.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../platforms/Th1520Pkg/Application/Fastboot/Image.h"
STATIC TH1520_IMAGE_POLICY Policy;
VOID *EFIAPI AllocateCopyPool (UINTN Size, CONST VOID *Buffer)
{
  VOID *Copy = malloc (Size);
  if (Copy != NULL) memcpy (Copy, Buffer, Size);
  return Copy;
}
VOID EFIAPI FreePool (VOID *Buffer) { free (Buffer); }
#define ValidImage(Image, Size) Th1520ValidImage ((Image), (Size), &Policy)
UINTN EFIAPI AsciiStrLen (CONST CHAR8 *S) { return strlen (S); }
UINTN EFIAPI AsciiStrSize (CONST CHAR8 *S) { return strlen (S) + 1; }
UINTN EFIAPI AsciiStrnLenS (CONST CHAR8 *S, UINTN N) { return strnlen (S, N); }
INTN EFIAPI AsciiStrCmp (CONST CHAR8 *A, CONST CHAR8 *B) { return strcmp (A, B); }
INTN EFIAPI
CompareMem (CONST VOID *A, CONST VOID *B, UINTN N)
{
  return memcmp (A, B, N);
}
BOOLEAN EFIAPI
Sha256HashAll (CONST VOID *Data, UINTN Size, UINT8 *Digest)
{
  return SHA256 (Data, Size, Digest) != NULL;
}
INT32 EFIAPI
FdtCheckHeader (CONST VOID *Fdt)
{
  return fdt_check_header (Fdt);
}
UINT32 EFIAPI
Fdt32ToCpu (UINT32 Value)
{
  return fdt32_to_cpu (Value);
}
INT32 EFIAPI
FdtPathOffset (CONST VOID *Fdt, CONST CHAR8 *Path)
{
  return fdt_path_offset (Fdt, Path);
}
INT32 EFIAPI
FdtSubnodeOffset (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Name)
{
  return fdt_subnode_offset (Fdt, Node, Name);
}
CONST VOID *EFIAPI
FdtGetProp (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Name, INT32 *Len)
{
  return fdt_getprop (Fdt, Node, Name, Len);
}
INT32 EFIAPI
FdtFirstSubnode (CONST VOID *Fdt, INT32 Node)
{
  return fdt_first_subnode (Fdt, Node);
}
INT32 EFIAPI
FdtNextSubnode (CONST VOID *Fdt, INT32 Node)
{
  return fdt_next_subnode (Fdt, Node);
}
int
main (int argc, char **argv)
{
  FILE *File;
  long Size;
  UINT8 *Image, *Original;
  void *Fit;
  int Node, Len;
  const fdt32_t *Position;
  UINT32 Offset;
  char ConfigPath[80];
  assert (argc == 4);
  Policy.Configuration = argv[2];
  Policy.Medium = argv[3];
  Policy.Compatible = !strcmp (argv[2], "meles") ? "milkv,meles" : "sipeed,lichee-pi-4a";
  Policy.MaximumSize = !strcmp (argv[2], "meles") ? SIZE_8MB : SIZE_4MB;
  snprintf (ConfigPath, sizeof (ConfigPath), "/configurations/%s", argv[2]);
  File = fopen (argv[1], "rb");
  assert (File);
  assert (!fseek (File, 0, SEEK_END));
  Size = ftell (File);
  rewind (File);
  Image = malloc (Size);
  Original = malloc (Size);
  assert (Image && Original);
  assert (fread (Original, 1, Size, File) == (size_t)Size);
  fclose (File);
  memcpy (Image, Original, Size);
  assert (ValidImage (Image, Size));
  {
    TH1520_IMAGE_POLICY Wrong = Policy;
    Wrong.Configuration = "wrong-board";
    assert (!Th1520ValidImage (Image, Size, &Wrong));
    Wrong = Policy;
    Wrong.Compatible = "unsupported,board";
    assert (!Th1520ValidImage (Image, Size, &Wrong));
    Wrong = Policy;
    Wrong.Medium = !strcmp (Policy.Medium, "emmc-boot0") ? "ram" : "emmc-boot0";
    assert (!Th1520ValidImage (Image, Size, &Wrong));
    Wrong = Policy;
    Wrong.MaximumSize = Size - 1;
    assert (!Th1520ValidImage (Image, Size, &Wrong));
  }
  assert (!ValidImage (Image, 0));
  assert (!ValidImage (Image, 0x40000));
  assert (!ValidImage (Image, 0x40000 + 39));
  assert (!ValidImage (Image, Policy.MaximumSize + 1));
  Fit = Image + 0x40000;
  Node = fdt_path_offset (Fit, "/images/edk2");
  assert (Node >= 0);
  Position = fdt_getprop (Fit, Node, "data-position", &Len);
  assert (Position && Len == 4);
  Offset = fdt32_to_cpu (*Position);
  assert (!ValidImage (Image, 0x40000 + Offset + 1024));
  Image[0x40000 + Offset + 1024] ^= 1;
  assert (!ValidImage (Image, Size));
  memcpy (Image, Original, Size);
  assert (!fdt_setprop_inplace_u32 (Fit, Node, "data-position", 0xfffffff0));
  assert (!ValidImage (Image, Size));
  memcpy (Image, Original, Size);
  assert (!fdt_setprop_inplace_u32 (Fit, Node, "data-size", 0xffffffff));
  assert (!ValidImage (Image, Size));
  memcpy (Image, Original, Size);
  Node = fdt_path_offset (Fit, "/images/edk2/hash");
  assert (Node >= 0);
  assert (!fdt_setprop_inplace (Fit, Node, "algo", "sha512", 7));
  assert (!ValidImage (Image, Size));
  memcpy (Image, Original, Size);
  assert (!fdt_del_node (Fit, Node));
  assert (!ValidImage (Image, Size));
  memcpy (Image, Original, Size);
  Node = fdt_path_offset (Fit, ConfigPath);
  assert (Node >= 0);
  assert (!fdt_delprop (Fit, Node, "description"));
  assert (!ValidImage (Image, Size));
  memcpy (Image, Original, Size);
  Node = fdt_path_offset (Fit, "/images/aon");
  assert (Node >= 0);
  assert (!fdt_del_node (Fit, Node));
  assert (!ValidImage (Image, Size));
  memcpy (Image, Original, Size);
  fdt_set_totalsize (Fit, Size);
  assert (!ValidImage (Image, Size));
  free (Original);
  free (Image);
  puts ("EDK2 image validation: all cases passed");
  return 0;
}
