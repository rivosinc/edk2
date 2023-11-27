/** @file
  Copyright (c) 2023, Intel Corporation. All rights reserved.<BR>
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

/**
  Add a new HOB to the HOB List.

  @param HobType            Type of the new HOB.
  @param HobLength          Length of the new HOB to allocate.

  @return  NULL if there is no space to create a hob.
  @return  The address point to the new created hob.

**/
VOID *
EFIAPI
CreateHob (
  IN  UINT16  HobType,
  IN  UINT16  HobLength
  );

/**
  Check the HOB and decide if it is need inside Payload
  Payload maintainer may make decision which HOB is need or needn't
  Then add the check logic in the function.
  @param[in] Hob The HOB to check
  @retval TRUE  If HOB is need inside Payload
  @retval FALSE If HOB is needn't inside Payload
**/
BOOLEAN
FitIsHobNeed (
  EFI_PEI_HOB_POINTERS  Hob
  )
{
  if (Hob.Header->HobType == EFI_HOB_TYPE_HANDOFF) {
    return FALSE;
  }

  if (Hob.Header->HobType == EFI_HOB_TYPE_MEMORY_ALLOCATION) {
    if (CompareGuid (&Hob.MemoryAllocationModule->MemoryAllocationHeader.Name, &gEfiHobMemoryAllocModuleGuid)) {
      return FALSE;
    }
  }

  if (Hob.Header->HobType == EFI_HOB_TYPE_MEMORY_ALLOCATION) {
    if (CompareGuid (&Hob.MemoryAllocation->AllocDescriptor.Name, &gUniversalPayloadDeviceTreeGuid)) {
      return FALSE;
    }

    if ((Hob.ResourceDescriptor->ResourceType == EFI_RESOURCE_MEMORY_MAPPED_IO) ||
        (Hob.ResourceDescriptor->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY) ||
        (Hob.ResourceDescriptor->ResourceType == EFI_RESOURCE_MEMORY_RESERVED))
    {
      return FALSE;
    }
  }

  if (Hob.Header->HobType == EFI_HOB_TYPE_GUID_EXTENSION) {
    if (CompareGuid (&Hob.Guid->Name, &gUniversalPayloadSerialPortInfoGuid)) {
      return FALSE;
    }
  }

  // Arrive here mean the HOB is need
  return TRUE;
}

/**
  It will Parse FDT -custom node based on information from bootloaders.
  @param[in]  FdtBase The starting memory address of FdtBase
  @param[in]  HostList The starting memory address of New Hob list.

**/
UINT64
CustomFdtNodeParser (
  IN VOID  *Fdt,
  IN VOID  *HostList
  )
{
  INT32               Node;
  INT32               TempLen;
  UINT64              *Data64;
  UINT64              CHobList;
  CONST FDT_PROPERTY  *PropertyPtr;

  CHobList = 0;

  DEBUG ((DEBUG_INFO, "%a() #1 \n", __func__));

  //
  // Look for if exists hob list node
  //
  Node = FdtSubnodeOffsetNameLen (Fdt, 0, "hoblistptr", (INT32)AsciiStrLen ("hoblistptr"));
  if (Node > 0) {
    PropertyPtr = FdtGetProperty (Fdt, Node, "hoblistptr", &TempLen);
    Data64      = (UINT64 *)(PropertyPtr->Data);
    CHobList    = Fdt64ToCpu (*Data64);
    DEBUG ((DEBUG_INFO, "  Found hob list node (%08X)", Node));
    DEBUG ((DEBUG_INFO, " -pointer  %016lX\n", CHobList));
  }

  return CHobList;

  DEBUG ((DEBUG_INFO, "%a() #3 \n", __func__));
}
