/** @file
  Copyright (c) 2024, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <PiPei.h>
#include <Pi/PiHob.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PrintLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>
#include <Library/PcdLib.h>
#include "../UefiPayloadEntry/UefiPayloadEntry.h"


/**
  It will Parse FDT -custom node based on information from bootloaders.
  @param[in]  FdtBase The starting memory address of FdtBase
  @param[in]  HobList The starting memory address of New Hob list.

**/
UINTN
EFIAPI
CustomFdtNodeParser (
  IN VOID  *FdtBase,
  IN VOID  *HobList
  )
{
  INT32               Node;
  INT32               Property;
  CONST FDT_PROPERTY  *PropertyPtr;
  INT32               TempLen;
  UINT32              *Data32;
  INT32               Len;
  EFI_HOB_RVCPU       *Hob;

  Node = FdtSubnodeOffsetNameLen (FdtBase, 0, "cpus", (INT32)AsciiStrLen ("cpus"));
  if (Node <= 0) {
    DEBUG ((DEBUG_INFO, "Could not find CPU node\n"));
  }

  if (FdtGetProperty (FdtBase, Node, "boot-hart", &Len) != 0) {
    Property    = FdtFirstPropertyOffset (FdtBase, Node);
    PropertyPtr = FdtGetPropertyByOffset (FdtBase, Property, &TempLen);
    Data32      = (UINT32 *)(PropertyPtr->Data);

    Hob = CreateHob (EFI_HOB_TYPE_RV_CPU, sizeof (EFI_HOB_RVCPU));
    ASSERT (Hob != NULL);
    if (Hob == NULL) {
      return RETURN_NOT_FOUND;
    }

    Hob->CpuId = (UINT8)Fdt32ToCpu (*Data32);

    //
    // Zero the reserved space to match HOB spec
    //
    ZeroMem (Hob->Reserved, sizeof (Hob->Reserved));
  }

  return EFI_SUCCESS;
}
