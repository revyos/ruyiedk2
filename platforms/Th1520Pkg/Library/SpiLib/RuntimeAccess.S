// SPDX-License-Identifier: BSD-2-Clause-Patent
// Use device PTEs even when the OS maps EFI MMIO as normal memory.
// No stack or data accesses occur under the temporary address space.
.section .text.Th1520SpiMmio, "ax", @progbits
.balign 4096
.globl Th1520SpiMmio
.type Th1520SpiMmio, @function
Th1520SpiMmio:
  csrrci t0, sstatus, 2
  csrr t1, satp
  fence iorw, iorw
  csrw satp, a2
  sfence.vma
  beqz a3, 1f
  sw a1, 0(a0)
  mv a0, a1
  j 2f
1:
  lwu a0, 0(a0)
2:
  fence iorw, iorw
  csrw satp, t1
  sfence.vma
  andi t0, t0, 2
  csrs sstatus, t0
  ret
.size Th1520SpiMmio, .-Th1520SpiMmio
