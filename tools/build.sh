#!/bin/bash
# SPDX-License-Identifier: BSD-2-Clause-Patent
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
edk2_source=$(cd "${EDK2_SOURCE:-$root/../edk2}" && pwd)
export WORKSPACE="$root/build"
export CONF_PATH="$WORKSPACE/Conf"
export EDK_TOOLS_PATH="$edk2_source/BaseTools"
export PACKAGES_PATH="$edk2_source:$root/platforms:$root"
export GCC_RISCV64_PREFIX=${CROSS_COMPILE:-riscv64-linux-gnu-}
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(
  git -C "$edk2_source" show -s --format=%ct HEAD
)}
case "${EDK2_BOARD:-meles}" in
  melonpi|melonpi-v1|melonpi-v2)
    platform=MelonPiPkg/MelonPi.dsc ;;
  meles) platform=MelesPkg/Meles.dsc ;;
  meles-recovery) platform=MelesPkg/MelesRecovery.dsc ;;
  lpi4a) platform=Lpi4aPkg/Lpi4a.dsc ;;
  lpi4a-recovery) platform=Lpi4aPkg/Lpi4aRecovery.dsc ;;
  *) echo "Unknown EDK2_BOARD: $EDK2_BOARD" >&2; exit 1 ;;
esac
mkdir -p "$CONF_PATH"
cd "$WORKSPACE"
edk2_build_args=("$@")
set --
source "$edk2_source/edksetup.sh"
build -a RISCV64 -t GCC -b "${EDK2_TARGET:-RELEASE}" \
  -p "$platform" -n "${JOBS:-8}" "${edk2_build_args[@]}"
