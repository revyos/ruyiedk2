# SPDX-License-Identifier: BSD-2-Clause-Patent
[Defines]
  DEFINE DEBUG_ON_SERIAL_PORT = TRUE
  DEFINE SECURE_BOOT_ENABLE = FALSE
  DEFINE NETWORK_ENABLE = TRUE
  DEFINE NETWORK_SNP_ENABLE = FALSE
  DEFINE NETWORK_IP4_ENABLE = TRUE
  DEFINE NETWORK_IP6_ENABLE = FALSE
  DEFINE NETWORK_HTTP_BOOT_ENABLE = FALSE
  DEFINE NETWORK_PXE_BOOT_ENABLE = TRUE
  DEFINE NETWORK_ISCSI_ENABLE = FALSE
  DEFINE NETWORK_VLAN_ENABLE = FALSE
  DEFINE NETWORK_TLS_ENABLE = FALSE
  DEFINE BUILD_SHELL = TRUE
  DEFINE ACPIVIEW_ENABLE = FALSE
!include MdePkg/MdeLibs.dsc.inc
!include NetworkPkg/Network.dsc.inc
!include OvmfPkg/RiscVVirt/RiscVVirt.dsc.inc
[BuildOptions]
  GCC:RELEASE_*_*_CC_FLAGS = -DMDEPKG_NDEBUG
[BuildOptions.common.EDKII.DXE_RUNTIME_DRIVER]
  GCC:*_*_*_DLINK_FLAGS = -z common-page-size=0x1000
[LibraryClasses.common]
  A210ScmiLib|A210Pkg/Library/ScmiLib/ScmiLib.inf
  ArmMtlLib|A210Pkg/Library/ScmiMtlLib/ScmiMtlLib.inf
  A210PciLib|A210Pkg/Library/PciLib/PciLib.inf
  PciHostBridgeLib|A210Pkg/Library/PciHostBridgeLib/PciHostBridgeLib.inf
  PciSegmentLib|MdePkg/Library/UefiPciSegmentLibPciRootBridgeIo/UefiPciSegmentLibPciRootBridgeIo.inf
  A210PeripheralLib|A210Pkg/Library/PeripheralLib/PeripheralLib.inf
  Fusb302Lib|RiscVPlatformPkg/Library/Fusb302Lib/Fusb302Lib.inf
  A210UsbHostLib|A210Pkg/Library/UsbHostLib/UsbHostLib.inf
  Dwc2HostPlatformLib|A210Pkg/Library/UsbHostLib/Dwc2HostPlatformLib.inf
  DmaLib|EmbeddedPkg/Library/NonCoherentDmaLib/NonCoherentDmaLib.inf
  DisplayConsoleLib|RiscVPlatformPkg/Library/DisplayConsoleLib/DisplayConsoleLib.inf
  DisplayPlatformLib|A210Pkg/Library/DisplayPlatformLib/DisplayPlatformLib.inf
  A210BootMediaLib|A210Pkg/Library/BootMediaLib/BootMediaLib.inf
  EqosPlatformLib|A210Pkg/Library/EqosPlatformLib/EqosPlatformLib.inf
  PlatformSmbiosLib|A210Pkg/Library/PlatformSmbiosLib/PlatformSmbiosLib.inf
  FastbootStorageLib|RiscVPlatformPkg/Library/FastbootStorageLib/FastbootStorageLib.inf
  FastbootPlatformLib|A210Pkg/Library/FastbootPlatformLib/FastbootPlatformLib.inf
  FastbootTransportLib|RiscVPlatformPkg/Library/Dwc2FastbootLib/Dwc2FastbootLib.inf
  UsbDevicePlatformLib|A210Pkg/Library/UsbDevicePlatformLib/UsbDevicePlatformLib.inf
  BootProgressLib|RiscVPlatformPkg/Library/BootProgressLib/BootProgressLib.inf
  BootLogoLib|MdeModulePkg/Library/BootLogoLib/BootLogoLib.inf
  FrameBufferBltLib|MdeModulePkg/Library/FrameBufferBltLib/FrameBufferBltLib.inf
  NonDiscoverableDeviceRegistrationLib|MdeModulePkg/Library/NonDiscoverableDeviceRegistrationLib/NonDiscoverableDeviceRegistrationLib.inf
  CustomizedDisplayLib|MdeModulePkg/Library/CustomizedDisplayLib/CustomizedDisplayLib.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  TpmMeasurementLib|MdeModulePkg/Library/TpmMeasurementLibNull/TpmMeasurementLibNull.inf
  ImagePropertiesRecordLib|MdeModulePkg/Library/ImagePropertiesRecordLib/ImagePropertiesRecordLib.inf
  PlatformSecLib|RiscVPlatformPkg/Library/PlatformSecLib/PlatformSecLib.inf
  PlatformResourceLib|A210Pkg/Library/PlatformResourceLib/PlatformResourceLib.inf
  PlatformBootManagerLib|RiscVPlatformPkg/Library/SerialBootManagerLib/BootManagerLib.inf
  PlatformHookLib|MdeModulePkg/Library/BasePlatformHookLibNull/BasePlatformHookLibNull.inf
  TimerLib|UefiCpuPkg/Library/RiscV64CpuTimerLib/RiscV64CpuTimerDxeLib.inf
  GptLib|MdeModulePkg/Library/GptLib/GptLib.inf
[LibraryClasses.common.SEC]
  TimerLib|UefiCpuPkg/Library/RiscV64CpuTimerLib/RiscV64CpuTimerSecLib.inf
[LibraryClasses.common.DXE_RUNTIME_DRIVER]
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
[PcdsFixedAtBuild]
!ifdef EMMC_DEBUG
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80400047
!endif
!ifdef GMAC_DEBUG
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80400047
!endif
  gEfiMdePkgTokenSpaceGuid.PcdEnforceSecureRngAlgorithms|FALSE
  gEfiMdeModulePkgTokenSpaceGuid.PcdSmbiosVersion|0x0308
  gEfiMdeModulePkgTokenSpaceGuid.PcdSmbiosEntryPointProvideMethod|0x2
  gEfiMdeModulePkgTokenSpaceGuid.PcdBootManagerMenuFile|{ 0x21, 0xaa, 0x2c, 0x46, 0x14, 0x76, 0x03, 0x45, 0x83, 0x6e, 0x8a, 0xb6, 0xf4, 0x66, 0x23, 0x31 }
  gEfiMdeModulePkgTokenSpaceGuid.PcdFirmwareVendor|L"$(BOARD_VENDOR)"
  gEfiMdeModulePkgTokenSpaceGuid.PcdFirmwareVersionString|L"$(FIRMWARE_NAME)"
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialUseMmio|TRUE
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialRegisterBase|0x08401000
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialRegisterStride|4
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialRegisterAccessWidth|32
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialClockRate|24000000
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialBaudRate|115200
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialUseHardwareFlowControl|FALSE
  gEfiMdeModulePkgTokenSpaceGuid.PcdImageProtectionPolicy|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdDxeNxMemoryProtectionPolicy|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdNullPointerDetectionPropertyMask|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdStatusCodeUseSerial|TRUE
  gEfiMdeModulePkgTokenSpaceGuid.PcdStatusCodeUseMemory|FALSE
  gEfiMdeModulePkgTokenSpaceGuid.PcdResetOnMemoryTypeInformationChange|FALSE
  gEfiMdePkgTokenSpaceGuid.PcdReportStatusCodePropertyMask|0x02
  gEfiMdePkgTokenSpaceGuid.PcdRiscVFeatureOverride|0x5
[PcdsDynamicDefault]
  gEfiMdeModulePkgTokenSpaceGuid.PcdVideoHorizontalResolution|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdVideoVerticalResolution|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutColumn|80
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutRow|25
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupVideoHorizontalResolution|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupVideoVerticalResolution|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutColumn|80
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutRow|25
  gEfiMdeModulePkgTokenSpaceGuid.PcdEmuVariableNvModeEnable|TRUE
[PcdsDynamicHii]
  gEfiMdePkgTokenSpaceGuid.PcdPlatformBootTimeOut|L"Timeout"|gEfiGlobalVariableGuid|0x0|10
[Components]
  A210Pkg/Drivers/ScmiDxe/ScmiDxe.inf
  ArmPkg/Drivers/ArmScmiDxe/ArmScmiDxe.inf
  UefiCpuPkg/SecCore/SecCoreNative.inf {
    <LibraryClasses>
      ExtractGuidedSectionLib|EmbeddedPkg/Library/PrePiExtractGuidedSectionLib/PrePiExtractGuidedSectionLib.inf
      NULL|MdeModulePkg/Library/LzmaCustomDecompressLib/LzmaCustomDecompressLib.inf
  }
  MdeModulePkg/Core/Dxe/DxeMain.inf {
    <LibraryClasses>
      DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  }
  MdeModulePkg/Universal/PCD/Dxe/Pcd.inf {
    <LibraryClasses>
      PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  }
  UefiCpuPkg/CpuDxeRiscV64/CpuDxeRiscV64.inf
  A210Pkg/Drivers/CpuDmaDxe/CpuDmaDxe.inf
  MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf
  MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf
  MdeModulePkg/Universal/MonotonicCounterRuntimeDxe/MonotonicCounterRuntimeDxe.inf
  MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
  EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  MdeModulePkg/Universal/Console/TerminalDxe/TerminalDxe.inf
  MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf
  MdeModulePkg/Universal/SerialDxe/SerialDxe.inf
  MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf
  MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf
  MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  MdeModulePkg/Universal/BdsDxe/BdsDxe.inf
  MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf
  MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf
  FatPkg/EnhancedFatDxe/Fat.inf
  UefiCpuPkg/CpuTimerDxeRiscV64/CpuTimerDxeRiscV64.inf
  EmbeddedPkg/Drivers/FdtClientDxe/FdtClientDxe.inf
  MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  MdeModulePkg/Universal/StatusCodeHandler/RuntimeDxe/StatusCodeHandlerRuntimeDxe.inf
  MdeModulePkg/Universal/Acpi/BootGraphicsResourceTableDxe/BootGraphicsResourceTableDxe.inf
  MdeModulePkg/Universal/DisplayEngineDxe/DisplayEngineDxe.inf
  MdeModulePkg/Universal/SetupBrowserDxe/SetupBrowserDxe.inf
  MdeModulePkg/Universal/DriverHealthManagerDxe/DriverHealthManagerDxe.inf
  MdeModulePkg/Application/BootManagerMenuApp/BootManagerMenuApp.inf
  MdeModulePkg/Application/UiApp/UiApp.inf {
    <LibraryClasses>
      NULL|MdeModulePkg/Library/DeviceManagerUiLib/DeviceManagerUiLib.inf
      NULL|MdeModulePkg/Library/BootManagerUiLib/BootManagerUiLib.inf
      NULL|MdeModulePkg/Library/BootMaintenanceManagerUiLib/BootMaintenanceManagerUiLib.inf
  }
  MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  MdeModulePkg/Universal/SmbiosDxe/SmbiosDxe.inf

!ifdef HSM_TEST
  tests/HsmTest/HsmTest.inf
!endif

!ifdef IO_TEST
  tests/IoTest/IoTest.inf
!endif

!ifdef NET_TEST
  tests/NetTest/NetTest.inf
!endif

[Components]
  A210Pkg/Drivers/EmmcHostDxe/EmmcHostDxe.inf {
!ifdef EMMC_DEBUG
    <BuildOptions>
      GCC:*_*_*_CC_FLAGS = -UMDEPKG_NDEBUG
!endif
  }
  MdeModulePkg/Bus/Pci/NonDiscoverablePciDeviceDxe/NonDiscoverablePciDeviceDxe.inf
  MdeModulePkg/Bus/Pci/SdMmcPciHcDxe/SdMmcPciHcDxe.inf {
!ifdef EMMC_DEBUG
    <BuildOptions>
      GCC:*_*_*_CC_FLAGS = -UMDEPKG_NDEBUG
!endif
  }
  MdeModulePkg/Bus/Sd/EmmcDxe/EmmcDxe.inf {
!ifdef EMMC_DEBUG
    <BuildOptions>
      GCC:*_*_*_CC_FLAGS = -UMDEPKG_NDEBUG
!endif
  }

!ifdef BLOCK_READ_TEST
  tests/BlockReadTest/BlockReadTest.inf
!endif

[Components]
  RiscVPlatformPkg/Application/Fastboot/Fastboot.inf
  RiscVPlatformPkg/Drivers/BootOptionPolicyDxe/BootOptionPolicyDxe.inf
  RiscVPlatformPkg/Drivers/SmbiosDxe/SmbiosDxe.inf

[Components]
  RiscVPlatformPkg/Drivers/DwMac4Dxe/DwMac4Dxe.inf {
    <BuildOptions>
      GCC:*_*_*_CC_FLAGS = -UMDEPKG_NDEBUG
  }
  RiscVPlatformPkg/Drivers/SoftwareRngDxe/SoftwareRngDxe.inf

[Components]
  RiscVPlatformPkg/Drivers/Dc8200Dxe/DisplayDxe.inf
  RiscVPlatformPkg/Drivers/LogoDxe/LogoDxe.inf
  MdeModulePkg/Universal/Acpi/BootGraphicsResourceTableDxe/BootGraphicsResourceTableDxe.inf

  A210Pkg/Drivers/UsbHostDxe/UsbHostDxe.inf
  RiscVPlatformPkg/Drivers/Dwc2HostDxe/Dwc2HostDxe.inf {
    <PcdsFixedAtBuild>
      gEmbeddedTokenSpaceGuid.PcdDmaDeviceLimit|0xffffffff
  }
  MdeModulePkg/Bus/Pci/XhciDxe/XhciDxe.inf
  MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf
  MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
  MdeModulePkg/Bus/Usb/UsbMouseDxe/UsbMouseDxe.inf

  RiscVPlatformPkg/Drivers/MmioCpuIo2Dxe/MmioCpuIo2Dxe.inf
  EmbeddedPkg/Drivers/NonCoherentIoMmuDxe/NonCoherentIoMmuDxe.inf
  MdeModulePkg/Bus/Pci/PciHostBridgeDxe/PciHostBridgeDxe.inf {
    <LibraryClasses>
      PciSegmentLib|A210Pkg/Library/PciSegmentLib/PciSegmentLib.inf
  }
  MdeModulePkg/Bus/Pci/PciBusDxe/PciBusDxe.inf
  MdeModulePkg/Bus/Pci/NvmExpressDxe/NvmExpressDxe.inf
