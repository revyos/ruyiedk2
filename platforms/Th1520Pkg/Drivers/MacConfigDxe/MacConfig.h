// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef TH1520_MAC_FORM_H
#define TH1520_MAC_FORM_H
#define MAC_FORM_GUID \
  {0x623937a4, 0x1db3, 0x4986, \
   {0x8a, 0xb5, 0xa7, 0x0d, 0x64, 0x3a, 0x9a, 0xc1}}
#define KEY_MAC0 0x1001
#define KEY_MAC1 0x1002
#define KEY_GENERATE 0x1003
#define KEY_AUTO 0x1004
#define KEY_SAVE 0x1005
#pragma pack(1)
typedef struct {
  UINT8 Present0;
  UINT8 Present1;
  UINT8 Locked0;
  UINT8 Locked1;
  UINT8 Use0;
  UINT8 Use1;
  UINT16 Address0[18];
  UINT16 Address1[18];
} MAC_FORM_DATA;
#pragma pack()
#endif
