/*
  Copyright (c) 2024 Rivos Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent

 */
.text
.align 3

#include <Register/RiscV64/RiscVImpl.h>
#include <Library/RiscV64/StandaloneMmCoreEntryPoint.h>
#include <Library/RiscV64/StandaloneMmRiscvSse.h>
#include <Library/BaseRiscVSbiLib.h>

ASM_FUNC (_SseEntryPoint)
  /*
   * a6: Event Arg
   * a7: Hart Id
   */
  move  a0, a6
  call  SbiSseEntryPoint

  li a7, SBI_EXT_SSE
  li a6, SBI_SSE_EVENT_COMPLETE
  ecall
