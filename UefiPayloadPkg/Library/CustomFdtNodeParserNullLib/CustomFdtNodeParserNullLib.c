/** @file
  Copyright (c) 2023, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <PiPei.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>

#include <Library/PrintLib.h>
#include <Library/FdtLib.h>

/**
  It will Parse FDT -custom node based on information from bootloaders.
  @param[in]  FdtBase The starting memory address of FdtBase.
  @param[in]  HostList The starting memory address of New Hob list.
  @retval HobList   The base address of Hoblist.

**/
UINT64
CustomFdtNodeParser (
  IN VOID  *Fdt,
  IN VOID  *HostList
  )
{
  UINT64  CHobList;

  CHobList = 0;

  return CHobList;
}
