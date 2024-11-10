/** @file
  Entry point to the Standalone MM Foundation when initialized during the SEC
  phase on RISCV platforms

Copyright (c) 2017 - 2021, Arm Ltd. All rights reserved.<BR>
Copyright (c) 2023, Intel Corporation. All rights reserved.<BR>
Copyright (c) 2024, Rivos Inc<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiMm.h>

#include <StandaloneMmCpu.h>
#include <Library/RiscV64/StandaloneMmCoreEntryPoint.h>

#include <PiPei.h>
#include <Guid/MmramMemoryReserve.h>
#include <Guid/MpInformation.h>

#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/BaseLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/SerialPortLib.h>
#include <Library/PcdLib.h>

#include <Library/BaseRiscVSbiLib.h>
#include <Library/DxeRiscvMpxy.h>

PI_MM_CPU_DRIVER_ENTRYPOINT  CpuDriverEntryPoint = NULL;
EFI_MMRAM_DESCRIPTOR         *NsCommBufMmramRange;

UINTN
RetrieveMMPayload (
  RPMI_MESSAGE  *RpmiMsg
  )
{
  return (UINTN)RpmiMsg->Data;
}

/**
  Retrieve a pointer to and print the boot information passed by privileged
  secure firmware.

  @param  [in] SharedBufAddress   The pointer memory shared with privileged
                                  firmware.

**/
EFI_RISCV_SMM_PAYLOAD_INFO *
GetAndPrintBootinformation (
  IN VOID  *PayloadInfoAddress
  )
{
  EFI_RISCV_SMM_PAYLOAD_INFO  *PayloadBootInfo;
  EFI_RISCV_SMM_CPU_INFO      *PayloadCpuInfo;
  UINTN                       Index;

  PayloadBootInfo = (EFI_RISCV_SMM_PAYLOAD_INFO *)PayloadInfoAddress;

  if (PayloadBootInfo == NULL) {
    DEBUG ((DEBUG_ERROR, "PayloadBootInfo NULL\n"));
    return NULL;
  }

  DEBUG ((DEBUG_INFO, "NumMmMemRegions - 0x%x\n", PayloadBootInfo->NumMmMemRegions));
  DEBUG ((DEBUG_INFO, "MmMemBase       - 0x%lx\n", PayloadBootInfo->MmMemBase));
  DEBUG ((DEBUG_INFO, "MmMemLimit      - 0x%lx\n", PayloadBootInfo->MmMemLimit));
  DEBUG ((DEBUG_INFO, "MmImageBase     - 0x%lx\n", PayloadBootInfo->MmImageBase));
  DEBUG ((DEBUG_INFO, "MmStackBase     - 0x%lx\n", PayloadBootInfo->MmStackBase));
  DEBUG ((DEBUG_INFO, "MmHeapBase      - 0x%lx\n", PayloadBootInfo->MmHeapBase));
  DEBUG ((DEBUG_INFO, "MmNsCommBufBase - 0x%lx\n", PayloadBootInfo->MmNsCommBufBase));
  DEBUG ((DEBUG_INFO, "MmSharedBufBase - 0x%lx\n", PayloadBootInfo->MmSharedBufBase));

  DEBUG ((DEBUG_INFO, "MmImageSize     - 0x%x\n", PayloadBootInfo->MmImageSize));
  DEBUG ((DEBUG_INFO, "MmPcpuStackSize - 0x%x\n", PayloadBootInfo->MmPcpuStackSize));
  DEBUG ((DEBUG_INFO, "MmHeapSize      - 0x%x\n", PayloadBootInfo->MmHeapSize));
  DEBUG ((DEBUG_INFO, "MmNsCommBufSize - 0x%x\n", PayloadBootInfo->MmNsCommBufSize));
  DEBUG ((DEBUG_INFO, "MmSharedBufSize - 0x%x\n", PayloadBootInfo->MmSharedBufSize));

  DEBUG ((DEBUG_INFO, "NumCpus         - 0x%x\n", PayloadBootInfo->NumCpus));

  PayloadCpuInfo = (EFI_RISCV_SMM_CPU_INFO *)&(PayloadBootInfo->CpuInfo);

  for (Index = 0; Index < PayloadBootInfo->NumCpus; Index++) {
    DEBUG ((DEBUG_INFO, "ProcessorId        - 0x%lx\n", PayloadCpuInfo[Index].ProcessorId));
    DEBUG ((DEBUG_INFO, "Package            - 0x%x\n", PayloadCpuInfo[Index].Package));
    DEBUG ((DEBUG_INFO, "Core               - 0x%x\n", PayloadCpuInfo[Index].Core));
  }

  return PayloadBootInfo;
}

#include <Library/DebugLib.h>

void
PrintRpmiMessageHeader (
  RPMI_MESSAGE_HEADER  *Header
  )
{
  DEBUG ((DEBUG_VERBOSE, "Print RPMI:\n"));
  DEBUG ((DEBUG_VERBOSE, "  ServiceGroupId: 0x%04x\n", Header->ServiceGroupId));
  DEBUG ((DEBUG_VERBOSE, "  ServiceId: 0x%02x\n", Header->ServiceId));
  DEBUG ((DEBUG_VERBOSE, "  Flags: 0x%02x\n", Header->Flags));
  DEBUG ((DEBUG_VERBOSE, "  Token: 0x%04x\n", Header->Token));
  DEBUG ((DEBUG_VERBOSE, "  DataLen: 0x%04x\n", Header->DataLen));
}

VOID
EFIAPI
SendMMComplete (
  IN UINTN                    ChannelId,
  IN RISCV_SMM_MSG_COMM_ARGS  *EventCompleteSvcArgs
  )
{
  EFI_STATUS  Status;
  UINTN       SmmMsgLen, SmmRespLen;

  SmmMsgLen = sizeof (RISCV_SMM_MSG_COMM_ARGS);
  Status    = SbiMpxySendMessage (
                ChannelId,
                RISCV_MSG_ID_SMM_EVENT_COMPLETE,
                EventCompleteSvcArgs,
                SmmMsgLen,
                EventCompleteSvcArgs,
                &SmmRespLen
                );
  if (EFI_ERROR (Status) || (SmmRespLen == 0)) {
    DEBUG (
      (
       DEBUG_ERROR,
       "DelegatedEventLoop: "
       "Failed to commuicate\n"
      )
      );
    Status = EFI_ACCESS_DENIED;
    ASSERT (0);
  }
}

VOID
PrintReqfwdRetrieveResp (
  REQFWD_RETRIEVE_RESP  *Resp
  )
{
  DEBUG ((DEBUG_VERBOSE, "Print REQFWD_RETRIEVE_RESP:\n"));
  DEBUG ((DEBUG_VERBOSE, "  Status: 0x%08x\n", Resp->Status));
  DEBUG ((DEBUG_VERBOSE, "  Remaining: 0x%08x\n", Resp->Remaining));
  DEBUG ((DEBUG_VERBOSE, "  Returned: 0x%08x\n", Resp->Returned));
  DEBUG ((DEBUG_VERBOSE, "  Offset: 0x%08x\n", Resp->Offset));
}

VOID
EFIAPI
RetrieveReqFwdMessage (
  IN UINTN              ChannelId,
  REQFWD_RETRIEVE_RESP  *ReqFwdResp
  )
{
  EFI_STATUS           Status;
  UINTN                SmmMsgLen, SmmRespLen;
  REQFWD_RETRIEVE_CMD  ReqFwdCmd;

  SmmMsgLen = sizeof (REQFWD_RETRIEVE_CMD);
  Status    = SbiMpxySendMessage (
                ChannelId,
                RPMI_REQFWD_RETRIEVE_CURRENT_MESSAGE,
                (VOID *)&ReqFwdCmd,
                SmmMsgLen,
                (VOID *)ReqFwdResp,
                &SmmRespLen
                );

  if (EFI_ERROR (Status) || (SmmRespLen == 0)) {
    DEBUG (
      (
       DEBUG_ERROR,
       "DelegatedEventLoop: "
       "Failed to commuicate\n"
      )
      );
    Status = EFI_ACCESS_DENIED;
    ASSERT (0);
  }
}

/**
  A loop to delegated events.

  @param  [in] EventCompleteSvcArgs   Pointer to the event completion arguments.

**/
VOID
EFIAPI
DelegatedEventLoop (
  IN UINTN                    CpuId,
  IN UINTN                    ChannelId,
  IN RISCV_SMM_MSG_COMM_ARGS  *EventCompleteSvcArgs
  )
{
  EFI_STATUS            Status = EFI_UNSUPPORTED;
  UINTN                 SmmStatus;
  //REQFWD_RETRIEVE_RESP  ReqFwdResp;
  UINTN                 InputBuffer = (UINTN)NsCommBufMmramRange->PhysicalStart;

  //  UINTN       SmmMsgLen, SmmRespLen;
  SendMMComplete (ChannelId, EventCompleteSvcArgs);

  while (TRUE) {
    // RetrieveReqFwdMessage (ChannelId, &ReqFwdResp);
    // PrintReqfwdRetrieveResp (&ReqFwdResp);
    InputBuffer = InputBuffer;
  //  +ReqFwdResp.Offset;
    Status      = CpuDriverEntryPoint (
                    0,
                    CpuId,
                    InputBuffer
                    );
    if (EFI_ERROR (Status)) {
      DEBUG (
        (
         DEBUG_ERROR,
         "Failed delegated Status 0x%x\n",
         Status
        )
        );
    }

    switch (Status) {
      case EFI_SUCCESS:
        SmmStatus = RISCV_SMM_RET_SUCCESS;
        break;
      case EFI_INVALID_PARAMETER:
        SmmStatus = RISCV_SMM_RET_INVALID_PARAMS;
        break;
      case EFI_ACCESS_DENIED:
        SmmStatus = RISCV_SMM_RET_DENIED;
        break;
      case EFI_OUT_OF_RESOURCES:
        SmmStatus = RISCV_SMM_RET_NO_MEMORY;
        break;
      case EFI_UNSUPPORTED:
        SmmStatus = RISCV_SMM_RET_NOT_SUPPORTED;
        break;
      default:
        SmmStatus = RISCV_SMM_RET_NOT_SUPPORTED;
        break;
    }

    EventCompleteSvcArgs->Arg0 = SmmStatus;
    DEBUG ((DEBUG_INFO, "Status %x\n", SmmStatus));
    SendMMComplete (ChannelId, EventCompleteSvcArgs);
  }
}

/**
  Initialize parameters to be sent via SMM call.

  @param[out]     InitMmFoundationSmmArgs  Args structure

**/
STATIC
VOID
InitRiscVSmmArgs (
  IN UINTN                     ChannelId,
  OUT RISCV_SMM_MSG_COMM_ARGS  *InitMmFoundationSmmArgs
  )
{
  if (SbiMpxyChannelOpen (ChannelId) != EFI_SUCCESS) {
    DEBUG (
      (
       DEBUG_ERROR,
       "InitRiscVSmmArgs: "
       "Failed to set shared memory\n"
      )
      );
    ASSERT (0);
  }

 #if 0
  EFI_STATUS  Status;
  VOID        *SbiShmem;
  UINT64      ShmemP;
  UINT32      PhysHiAddress;
  UINT32      PhysLoAddress;

  //
  // Allocate memory to be shared with OpenSBI for MPXY
  //
  SbiShmem = AllocateAlignedPages (
               EFI_SIZE_TO_PAGES (RISCV_SMM_MSG_SHMEM_SIZE),
               RISCV_SMM_MSG_SHMEM_SIZE                     // Align
               );
  if (SbiShmem == NULL) {
    ASSERT (0);
  }

  ZeroMem (SbiShmem, RISCV_SMM_MSG_SHMEM_SIZE);
  ShmemP        = (UINT64)(SbiShmem);
  PhysHiAddress = (UINT32)(ShmemP >> 32);
  PhysLoAddress = (UINT32)(ShmemP & 0xFFFFFFFF);
  Status        = SbiMpxySetShmem (PhysHiAddress, PhysLoAddress, RISCV_SMM_MSG_SHMEM_SIZE);
  if (EFI_ERROR (Status)) {
    DEBUG (
      (
       DEBUG_ERROR,
       "DelegatedEventLoop: "
       "Failed to set shared memory\n"
      )
      );
    Status = EFI_ACCESS_DENIED;
    ASSERT (0);
  }

 #endif
  InitMmFoundationSmmArgs->Arg0               = RISCV_SMM_RET_SUCCESS;
  InitMmFoundationSmmArgs->Arg1               = 0;
  // InitMmFoundationSmmArgs->hdr.DataLen        = 0;
  // InitMmFoundationSmmArgs->hdr.Flags          = 0;
  // InitMmFoundationSmmArgs->hdr.ServiceGroupId = 0;
  // InitMmFoundationSmmArgs->hdr.ServiceId      = 0;
  // InitMmFoundationSmmArgs->hdr.Token          = 0;
}

/** Returns the HOB data for the matching HOB GUID.

  @param  [in]  HobList  Pointer to the HOB list.
  @param  [in]  HobGuid  The GUID for the HOB.
  @param  [out] HobData  Pointer to the HOB data.

  @retval  EFI_SUCCESS            The function completed successfully.
  @retval  EFI_INVALID_PARAMETER  Invalid parameter.
  @retval  EFI_NOT_FOUND          Could not find HOB with matching GUID.
**/
EFI_STATUS
GetGuidedHobData (
  IN  VOID            *HobList,
  IN  CONST EFI_GUID  *HobGuid,
  OUT VOID            **HobData
  )
{
  EFI_HOB_GUID_TYPE  *Hob;

  if ((HobList == NULL) || (HobGuid == NULL) || (HobData == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Hob = GetNextGuidHob (HobGuid, HobList);
  if (Hob == NULL) {
    return EFI_NOT_FOUND;
  }

  *HobData = GET_GUID_HOB_DATA (Hob);
  if (*HobData == NULL) {
    return EFI_NOT_FOUND;
  }

  return EFI_SUCCESS;
}

/**
  The entry point of Standalone MM Foundation.

  @param  [in]  CpuId             The Id assigned to this running CPU
  @param  [in]  BootInfoAddress   The address of boot info

**/
VOID
EFIAPI
CModuleEntryPoint (
  IN UINT64  CpuId,
  IN VOID    *PayloadInfoAddress
  )
{
  EFI_RISCV_SMM_PAYLOAD_INFO  *PayloadBootInfo;
  RISCV_SMM_MSG_COMM_ARGS     InitMmFoundationSmmArgs;
  VOID                        *HobStart;
  EFI_STATUS                  Status;

  PayloadBootInfo = GetAndPrintBootinformation (PayloadInfoAddress);
  if (PayloadBootInfo == NULL) {
    return;
  }

  //
  // Create Hoblist based upon boot information passed by privileged software
  //
  HobStart = CreateHobListFromBootInfo (&CpuDriverEntryPoint, PayloadBootInfo);

  //
  // Call the MM Core entry point
  //
  ProcessModuleEntryPointList (HobStart);

  DEBUG ((DEBUG_INFO, "Cpu Driver EP %p\n", (VOID *)CpuDriverEntryPoint));

  ZeroMem (&InitMmFoundationSmmArgs, sizeof (InitMmFoundationSmmArgs));
  InitRiscVSmmArgs (PayloadBootInfo->MpxyChannelId, &InitMmFoundationSmmArgs);

  // Find the descriptor that contains the whereabouts of the buffer for
  // communication with the Normal world.
  Status = GetGuidedHobData (
             HobStart,
             &gEfiStandaloneMmNonSecureBufferGuid,
             (VOID **)&NsCommBufMmramRange
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "NsCommBufMmramRange HOB data extraction failed - 0x%x\n", Status));
  }

  DEBUG ((DEBUG_INFO, "mNsCommBuffer.PhysicalStart - 0x%lx\n", (UINTN)NsCommBufMmramRange->PhysicalStart));
  DEBUG ((DEBUG_INFO, "mNsCommBuffer.PhysicalSize - 0x%lx\n", (UINTN)NsCommBufMmramRange->PhysicalSize));

  DelegatedEventLoop (CpuId, PayloadBootInfo->MpxyChannelId, &InitMmFoundationSmmArgs);
}
