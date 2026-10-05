# SPDX-License-Identifier: BSD-2-Clause-Patent
[Defines]
!ifndef RECOVERY_BUILD
  DEFINE RECOVERY_BUILD = FALSE
!endif
  DEFINE DEBUG_ON_SERIAL_PORT = TRUE
  DEFINE SECURE_BOOT_ENABLE = FALSE
!if $(RECOVERY_BUILD) == TRUE
  DEFINE NETWORK_ENABLE = FALSE
!else
  DEFINE NETWORK_ENABLE = TRUE
!endif
  DEFINE NETWORK_SNP_ENABLE = FALSE
  DEFINE NETWORK_IP4_ENABLE = TRUE
  DEFINE NETWORK_IP6_ENABLE = FALSE
  DEFINE NETWORK_HTTP_BOOT_ENABLE = FALSE
  DEFINE NETWORK_PXE_BOOT_ENABLE = TRUE
  DEFINE NETWORK_ISCSI_ENABLE = FALSE
  DEFINE NETWORK_VLAN_ENABLE = FALSE
  DEFINE NETWORK_TLS_ENABLE = FALSE
!if $(RECOVERY_BUILD) == TRUE
  DEFINE BUILD_SHELL = FALSE
!else
  DEFINE BUILD_SHELL = TRUE
!endif
  DEFINE ACPIVIEW_ENABLE = FALSE
!include MdePkg/MdeLibs.dsc.inc
!include NetworkPkg/Network.dsc.inc
!include OvmfPkg/RiscVVirt/RiscVVirt.dsc.inc
[BuildOptions]
  GCC:RELEASE_*_*_CC_FLAGS = -DMDEPKG_NDEBUG
[BuildOptions.common.EDKII.DXE_RUNTIME_DRIVER]
  GCC:*_*_*_DLINK_FLAGS = -z common-page-size=0x1000
[LibraryClasses.common]
  DisplayConsoleLib|RiscVPlatformPkg/Library/DisplayConsoleLib/DisplayConsoleLib.inf
  DisplayPlatformLib|Th1520Pkg/Library/DisplayPlatformLib/DisplayPlatformLib.inf
  Th1520MacStoreLib|Th1520Pkg/Library/MacStoreLib/MacStoreLib.inf
  Th1520NvStoreLib|Th1520Pkg/Library/NvStoreLib/NvStoreLib.inf
  Th1520SpiLib|Th1520Pkg/Library/SpiLib/SpiLib.inf
  Th1520ByteStorageLib|Th1520Pkg/Library/ByteStorageLib/ByteStorage.inf
  BootProgressLib|RiscVPlatformPkg/Library/BootProgressLib/BootProgressLib.inf
  BootLogoLib|MdeModulePkg/Library/BootLogoLib/BootLogoLib.inf
  FrameBufferBltLib|MdeModulePkg/Library/FrameBufferBltLib/FrameBufferBltLib.inf
  Th1520BoardLib|$(BOARD_LIBRARY)
  Th1520BoardSupportLib|Th1520Pkg/Library/BoardSupportLib/BoardSupportLib.inf
  NonDiscoverableDeviceRegistrationLib|MdeModulePkg/Library/NonDiscoverableDeviceRegistrationLib/NonDiscoverableDeviceRegistrationLib.inf
  CustomizedDisplayLib|MdeModulePkg/Library/CustomizedDisplayLib/CustomizedDisplayLib.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  TpmMeasurementLib|MdeModulePkg/Library/TpmMeasurementLibNull/TpmMeasurementLibNull.inf
  ImagePropertiesRecordLib|MdeModulePkg/Library/ImagePropertiesRecordLib/ImagePropertiesRecordLib.inf
  PlatformSecLib|RiscVPlatformPkg/Library/PlatformSecLib/PlatformSecLib.inf
  PlatformResourceLib|Th1520Pkg/Library/PlatformResourceLib/PlatformResourceLib.inf
!if $(RECOVERY_BUILD) == TRUE
  PlatformBootManagerLib|Th1520Pkg/Library/RecoveryBootManagerLib/RecoveryBootManagerLib.inf
!else
  PlatformBootManagerLib|Th1520Pkg/Library/BootManagerLib/BootManagerLib.inf
!endif
  RiscVMmuLib|Th1520Pkg/Library/BareMmuLib/BareMmuLib.inf
  CacheMaintenanceLib|Th1520Pkg/Library/CacheMaintenanceLib/CacheMaintenanceLib.inf
  PlatformHookLib|MdeModulePkg/Library/BasePlatformHookLibNull/BasePlatformHookLibNull.inf
  TimerLib|UefiCpuPkg/Library/RiscV64CpuTimerLib/RiscV64CpuTimerDxeLib.inf
  GptLib|MdeModulePkg/Library/GptLib/GptLib.inf
[LibraryClasses.common.SEC]
  TimerLib|UefiCpuPkg/Library/RiscV64CpuTimerLib/RiscV64CpuTimerSecLib.inf
[LibraryClasses.common.DXE_RUNTIME_DRIVER]
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
  ResetSystemLib|Th1520Pkg/Library/ResetSystemLib/ResetSystemLib.inf
[PcdsFixedAtBuild]
!if $(RECOVERY_BUILD) == TRUE
  # No forms browser is included in the serial recovery image.
  gEfiMdeModulePkgTokenSpaceGuid.PcdDriverHealthConfigureForm|{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
!endif
  gTh1520TokenSpaceGuid.PcdRecoveryMode|$(RECOVERY_BUILD)
  gTh1520TokenSpaceGuid.PcdBoardManufacturer|"$(BOARD_VENDOR)"
  gTh1520TokenSpaceGuid.PcdBoardProduct|"$(BOARD_PRODUCT)"
  gTh1520TokenSpaceGuid.PcdFirmwareName|"$(FIRMWARE_NAME)"
  gTh1520TokenSpaceGuid.PcdFastbootProduct|"$(FASTBOOT_PRODUCT)"
  gTh1520TokenSpaceGuid.PcdFitConfiguration|"$(FIT_CONFIGURATION)"
  gTh1520TokenSpaceGuid.PcdBoardCompatible|"$(BOARD_COMPATIBLE)"
  gTh1520TokenSpaceGuid.PcdFirmwareImageLimit|$(IMAGE_LIMIT)
  gTh1520TokenSpaceGuid.PcdFirmwareOnEmmc|$(FIRMWARE_ON_EMMC)
  # No S-mode TRNG provider: network identifiers use the declared software RNG.
  gEfiMdePkgTokenSpaceGuid.PcdEnforceSecureRngAlgorithms|FALSE
  gEfiMdeModulePkgTokenSpaceGuid.PcdSmbiosVersion|0x0308
  gEfiMdeModulePkgTokenSpaceGuid.PcdSmbiosEntryPointProvideMethod|0x2
  gEfiMdeModulePkgTokenSpaceGuid.PcdBootManagerMenuFile|{ 0x21, 0xaa, 0x2c, 0x46, 0x14, 0x76, 0x03, 0x45, 0x83, 0x6e, 0x8a, 0xb6, 0xf4, 0x66, 0x23, 0x31 }
  gEfiMdeModulePkgTokenSpaceGuid.PcdFirmwareVendor|L"$(BOARD_VENDOR)"
  gEfiMdeModulePkgTokenSpaceGuid.PcdFirmwareVersionString|L"$(FIRMWARE_NAME)"
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialUseMmio|TRUE
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialRegisterBase|0xffe7014000
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialRegisterStride|4
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialRegisterAccessWidth|32
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialClockRate|100000000
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialBaudRate|115200
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialUseHardwareFlowControl|FALSE
  gEfiMdeModulePkgTokenSpaceGuid.PcdImageProtectionPolicy|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdDxeNxMemoryProtectionPolicy|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdNullPointerDetectionPropertyMask|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdStatusCodeUseSerial|TRUE
  gEfiMdeModulePkgTokenSpaceGuid.PcdStatusCodeUseMemory|FALSE
  gEfiMdeModulePkgTokenSpaceGuid.PcdResetOnMemoryTypeInformationChange|FALSE
  gEfiMdePkgTokenSpaceGuid.PcdReportStatusCodePropertyMask|0x02
  gEfiMdePkgTokenSpaceGuid.PcdRiscVFeatureOverride|0
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
!if $(RECOVERY_BUILD) == TRUE
  gEfiMdePkgTokenSpaceGuid.PcdPlatformBootTimeOut|0
!else
[PcdsDynamicHii]
  gEfiMdePkgTokenSpaceGuid.PcdPlatformBootTimeOut|L"Timeout"|gEfiGlobalVariableGuid|0x0|10
!endif
[Components]
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
  Th1520Pkg/Application/Fastboot/Fastboot.inf
  Th1520Pkg/Drivers/EmmcDxe/EmmcDxe.inf
  MdeModulePkg/Bus/Pci/NonDiscoverablePciDeviceDxe/NonDiscoverablePciDeviceDxe.inf
  MdeModulePkg/Bus/Pci/XhciDxe/XhciDxe.inf
  MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf
  MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
  MdeModulePkg/Bus/Usb/UsbMouseDxe/UsbMouseDxe.inf
  Th1520Pkg/Drivers/UsbHostDxe/UsbHostDxe.inf
  Th1520Pkg/Drivers/NvStoreDxe/NvStoreDxe.inf
  Th1520Pkg/Drivers/GmacDxe/GmacDxe.inf
  Th1520Pkg/Drivers/MacConfigDxe/MacConfigDxe.inf
  Th1520Pkg/Drivers/Ip4ConfigDxe/Ip4ConfigDxe.inf
  RiscVPlatformPkg/Drivers/Dc8200Dxe/DisplayDxe.inf
  RiscVPlatformPkg/Drivers/LogoDxe/LogoDxe.inf
  MdeModulePkg/Universal/Acpi/BootGraphicsResourceTableDxe/BootGraphicsResourceTableDxe.inf
  RiscVPlatformPkg/Drivers/SoftwareRngDxe/SoftwareRngDxe.inf
  MdeModulePkg/Universal/DisplayEngineDxe/DisplayEngineDxe.inf
  MdeModulePkg/Universal/SetupBrowserDxe/SetupBrowserDxe.inf
  MdeModulePkg/Universal/DriverHealthManagerDxe/DriverHealthManagerDxe.inf
  MdeModulePkg/Application/BootManagerMenuApp/BootManagerMenuApp.inf
!if $(RECOVERY_BUILD) == FALSE
  MdeModulePkg/Application/UiApp/UiApp.inf {
    <LibraryClasses>
      NULL|MdeModulePkg/Library/DeviceManagerUiLib/DeviceManagerUiLib.inf
      NULL|MdeModulePkg/Library/BootManagerUiLib/BootManagerUiLib.inf
      NULL|MdeModulePkg/Library/BootMaintenanceManagerUiLib/BootMaintenanceManagerUiLib.inf
  }
!endif
  MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  MdeModulePkg/Universal/SmbiosDxe/SmbiosDxe.inf
  Th1520Pkg/Drivers/SmbiosDxe/SmbiosDxe.inf

!ifdef HSM_TEST
  tests/HsmTest/HsmTest.inf
!endif

!ifdef IO_TEST
  tests/IoTest/IoTest.inf
!endif

!ifdef NET_TEST
  tests/NetTest/NetTest.inf
!endif
