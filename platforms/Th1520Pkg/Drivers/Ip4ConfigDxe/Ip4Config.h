// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef IP4_SETUP_H
#define IP4_SETUP_H

#define IP4_FORM_GUID \
  {0xe1166979, 0xda87, 0x48c5, \
   {0x88, 0x59, 0x86, 0x02, 0x62, 0x13, 0x90, 0x64}}
#define IP4_TEXT_SIZE 16
#define IP4_DNS_COUNT 16
#define IP4_DNS_SIZE 256
#define KEY_SAVE 0x1001

#pragma pack(1)
typedef struct {
  UINT8 Dhcp;
  UINT8 Reserved;
  UINT16 Address[IP4_TEXT_SIZE];
  UINT16 Mask[IP4_TEXT_SIZE];
  UINT16 Gateway[IP4_TEXT_SIZE];
  UINT16 Dns[IP4_DNS_SIZE];
} IP4_FORM_DATA;
#pragma pack()

#ifndef VFRCOMPILE
#include <Protocol/Ip4Config2.h>
EFI_STATUS Ip4Load (EFI_IP4_CONFIG2_PROTOCOL *Config,
                    IP4_FORM_DATA *Data);
EFI_STATUS Ip4Save (EFI_IP4_CONFIG2_PROTOCOL *Config,
                    CONST IP4_FORM_DATA *Data);
#endif
#endif
