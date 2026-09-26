/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2011 Freescale Semiconductor, Inc.
 * Register subset from RevyOS 7.3.y drivers/gpu/drm/bridge/synopsys/dw-hdmi.h.
 */
#ifndef TH1520_DW_HDMI_REGS_H
#define TH1520_DW_HDMI_REGS_H

#define HDMI_A_HDCPCFG0                            0x5000
#define HDMI_A_HDCPCFG1                            0x5001
#define HDMI_FC_AVICONF0                           0x1019
#define HDMI_FC_AVICONF1                           0x101A
#define HDMI_FC_AVICONF2                           0x101B
#define HDMI_FC_AVICONF3                           0x1017
#define HDMI_FC_AVIVID                             0x101C
#define HDMI_FC_CH0PREAM                           0x1014
#define HDMI_FC_CH1PREAM                           0x1015
#define HDMI_FC_CH2PREAM                           0x1016
#define HDMI_FC_CTRLDUR                            0x1011
#define HDMI_FC_EXCTRLDUR                          0x1012
#define HDMI_FC_EXCTRLSPAC                         0x1013
#define HDMI_FC_HSYNCINDELAY0                      0x1008
#define HDMI_FC_HSYNCINWIDTH0                      0x100A
#define HDMI_FC_INHACTV0                           0x1001
#define HDMI_FC_INHBLANK0                          0x1003
#define HDMI_FC_INVACTV0                           0x1005
#define HDMI_FC_INVBLANK                           0x1007
#define HDMI_FC_INVIDCONF                          0x1000
#define HDMI_FC_PRCONF                             0x10E0
#define HDMI_FC_VSYNCINDELAY                       0x100C
#define HDMI_FC_VSYNCINWIDTH                       0x100D
#define HDMI_I2CM_ADDRESS                          0x7E01
#define HDMI_I2CM_CTLINT                           0x7E06
#define HDMI_I2CM_DATAI                            0x7E03
#define HDMI_I2CM_DIV                              0x7E07
#define HDMI_I2CM_INT                              0x7E05
#define HDMI_I2CM_OPERATION                        0x7E04
#define HDMI_I2CM_SEGADDR                          0x7E08
#define HDMI_I2CM_SEGPTR                           0x7E0A
#define HDMI_I2CM_SLAVE                            0x7E00
#define HDMI_I2CM_SOFTRSTZ                         0x7E09
#define HDMI_I2CM_SS_SCL_HCNT_0_ADDR               0x7E0C
#define HDMI_I2CM_SS_SCL_HCNT_1_ADDR               0x7E0B
#define HDMI_I2CM_SS_SCL_LCNT_0_ADDR               0x7E0E
#define HDMI_I2CM_SS_SCL_LCNT_1_ADDR               0x7E0D
#define HDMI_IH_I2CMPHY_STAT0                      0x0108
#define HDMI_IH_I2CM_STAT0                         0x0105
#define HDMI_IH_MUTE                               0x01FF
#define HDMI_IH_MUTE_I2CM_STAT0                    0x0185
#define HDMI_IH_PHY_STAT0                          0x0104
#define HDMI_MC_CLKDIS                             0x4001
#define HDMI_MC_FLOWCTRL                           0x4004
#define HDMI_MC_HEACPHY_RST                        0x4007
#define HDMI_MC_PHYRSTZ                            0x4005
#define HDMI_MC_SWRSTZ                             0x4002
#define HDMI_PHY_CONF0                             0x3000
#define HDMI_PHY_HPD                               0x02
#define HDMI_PHY_I2CM_ADDRESS_ADDR                 0x3021
#define HDMI_PHY_I2CM_CTLINT_ADDR                  0x3028
#define HDMI_PHY_I2CM_DATAO_0_ADDR                 0x3023
#define HDMI_PHY_I2CM_DATAO_1_ADDR                 0x3022
#define HDMI_PHY_I2CM_INT_ADDR                     0x3027
#define HDMI_PHY_I2CM_OPERATION_ADDR               0x3026
#define HDMI_PHY_I2CM_SLAVE_ADDR                   0x3020
#define HDMI_PHY_MASK0                             0x3006
#define HDMI_PHY_POL0                              0x3007
#define HDMI_PHY_STAT0                             0x3004
#define HDMI_PHY_TST0                              0x3001
#define HDMI_PHY_TX_PHY_LOCK                       0x01
#define HDMI_TX_BCBDATA1                           0x0207
#define HDMI_TX_GYDATA0                            0x0202
#define HDMI_TX_INSTUFFING                         0x0201
#define HDMI_TX_INVID0                             0x0200
#define HDMI_VP_CONF                               0x0804
#define HDMI_VP_PR_CD                              0x0801
#define HDMI_VP_REMAP                              0x0803
#define HDMI_VP_STUFF                              0x0802
#endif
