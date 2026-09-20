// SPDX-License-Identifier: BSD-2-Clause-Patent
.text
.balign 4
.globl HsmSecondary
.type HsmSecondary, @function
HsmSecondary:
  // SBI supplies a0=hart ID, a1=opaque. No stack or UEFI calls on APs.
  mv s0, a1
  sd a0, 16(s0)
  ld t0, 0(s0)
  sd t0, 8(s0)
  fence rw, w
  li t0, 1
  sd t0, 24(s0)
1:
  ld t0, 32(s0)
  beqz t0, 1b
  fence r, rw
  li a7, 0x48534d
  li a6, 1
  ecall
  // A successful hart_stop never returns.
  sd a0, 40(s0)
2:
  wfi
  j 2b

.size HsmSecondary, .-HsmSecondary
