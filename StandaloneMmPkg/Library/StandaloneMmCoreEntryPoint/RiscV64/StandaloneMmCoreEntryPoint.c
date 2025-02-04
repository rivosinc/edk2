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
#include <Library/RiscV64/StandaloneMmRiscvSse.h>

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

/** RPMI Messages Types */
enum rpmi_message_type {
  /* Normal request backed with ack */
  RPMI_MSG_NORMAL_REQUEST = 0x0,
  /* Request without any ack */
  RPMI_MSG_POSTED_REQUEST = 0x1,
  /* Acknowledgment for normal request message */
  RPMI_MSG_ACKNOWLDGEMENT = 0x2,
  /* Notification message */
  RPMI_MSG_NOTIFICATION = 0x3,
};

/*
 * RPMI SERVICEGROUPS AND SERVICES
 */

/** RPMI ServiceGroups IDs */
enum rpmi_servicegroup_id {
  RPMI_SRVGRP_ID_MIN          = 0,
  RPMI_SRVGRP_BASE            = 0x00001,
  RPMI_SRVGRP_SYSTEM_RESET    = 0x00002,
  RPMI_SRVGRP_SYSTEM_SUSPEND  = 0x00003,
  RPMI_SRVGRP_HSM             = 0x00004,
  RPMI_SRVGRP_CPPC            = 0x00005,
  RPMI_SRVGRP_CLOCK           = 0x00007,
  RPMI_SRVGRP_REQUEST_FORWARD = 0xC,
  RPMI_SRVGRP_ID_MAX_COUNT,
};

/** RPMI Error Types */
enum rpmi_error {
  RPMI_SUCCESS        = 0,
  RPMI_ERR_FAILED     = -1,
  RPMI_ERR_NOTSUPP    = -2,
  RPMI_ERR_INVAL      = -3,
  RPMI_ERR_DENIED     = -4,
  RPMI_ERR_NOTFOUND   = -5,
  RPMI_ERR_OUTOFRANGE = -6,
  RPMI_ERR_OUTOFRES   = -7,
  RPMI_ERR_HWFAULT    = -8,
};

/** RPMI ReqFwd ServiceGroup Service IDs */
enum rpmi_reqfwd_service_id {
  RPMI_REQFWD_ENABLE_NOTIFICATION      = 1,
  RPMI_REQFWD_RETRIEVE_CURRENT_MESSAGE = 2,
  RPMI_REQFWD_COMPLETE_CURRENT_MESSAGE = 3,
};

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

STATIC VOID
PrepareRpmiHeader (
  RPMI_MESSAGE_HEADER  *Hdr,
  UINT8                Msg
  )
{
  Hdr->Flags          = RPMI_MSG_NORMAL_REQUEST;
  Hdr->ServiceGroupId = RPMI_SRVGRP_REQUEST_FORWARD;
  Hdr->ServiceId      = Msg;
}

VOID
PrintReqfwdRetrieveResp (
  RPMI_SMM_MSG_COMM_ARGS  *Resp
  )
{
  PrintRpmiMessageHeader (&(Resp->rpmi_resp.hdr));
  DEBUG ((DEBUG_VERBOSE, "Print REQFWD_RETRIEVE_RESP:\n"));
  DEBUG ((DEBUG_VERBOSE, "  Status: 0x%08x\n", Resp->rpmi_resp.reqfwd_resp.Remaining));
  DEBUG ((DEBUG_VERBOSE, "  Remaining: 0x%08x\n", Resp->rpmi_resp.reqfwd_resp.Returned));
  DEBUG ((DEBUG_VERBOSE, "  Returned: 0x%08x\n", Resp->rpmi_resp.reqfwd_resp.Status));
}

VOID
EFIAPI
SendMMComplete (
  IN UINTN                  ChannelId,
  IN RPMI_SMM_MSG_CMPL_CMD  *EventCompleteSvcArgs
  )
{
  EFI_STATUS  Status;
  UINTN       SmmMsgLen, SmmRespLen;

  PrepareRpmiHeader (&EventCompleteSvcArgs->hdr, REQFWD_COMPLETE_CURRENT_MESSAGE);
  SmmMsgLen                          = sizeof (RPMI_SMM_MSG_CMPL_CMD);
  EventCompleteSvcArgs->mm_data.Arg0 = EFI_SUCCESS;
  EventCompleteSvcArgs->mm_data.Arg1 = 0;

  Status = SbiMpxySendMessage (
             ChannelId,
             REQFWD_COMPLETE_CURRENT_MESSAGE,
             EventCompleteSvcArgs,
             SmmMsgLen,
             EventCompleteSvcArgs,
             &SmmRespLen
             );
  if (EFI_ERROR (Status)) {
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
EFIAPI
RetrieveReqFwdMessage (
  IN UINTN                ChannelId,
  RPMI_SMM_MSG_COMM_ARGS  *pReqFwdResp
  )
{
  EFI_STATUS           Status;
  UINTN                SmmMsgLen, SmmRespLen;
  REQFWD_RETRIEVE_CMD  ReqFwdCmd;

  SmmRespLen = 0;
  PrepareRpmiHeader (&ReqFwdCmd.hdr, RPMI_REQFWD_RETRIEVE_CURRENT_MESSAGE);
  SmmMsgLen = sizeof (REQFWD_RETRIEVE_CMD);
  Status    = SbiMpxySendMessage (
                ChannelId,
                RPMI_REQFWD_RETRIEVE_CURRENT_MESSAGE,
                (VOID *)&ReqFwdCmd,
                SmmMsgLen,
                (VOID *)pReqFwdResp,
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
  IN UINTN                  CpuId,
  IN UINTN                  ChannelId,
  IN RPMI_SMM_MSG_CMPL_CMD  *EventCompleteSvcArgs
  )
{
  EFI_STATUS              Status = EFI_UNSUPPORTED;
  UINTN                   SmmStatus;
  RPMI_SMM_MSG_COMM_ARGS  MmReqFwdResp;

  RetrieveReqFwdMessage (ChannelId, &MmReqFwdResp);
  PrintReqfwdRetrieveResp (&MmReqFwdResp);

// Passing 0 in the entry enforces it to take sync MM flow.
  Status      = CpuDriverEntryPoint (
                  0,
                  CpuId,
                  0
                  );

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

  EventCompleteSvcArgs->mm_data.Arg0 = SmmStatus;
  DEBUG ((DEBUG_INFO, "Status %x\n", SmmStatus));
  SendMMComplete (ChannelId, EventCompleteSvcArgs);
}

/**
  Initialize parameters to be sent via SMM call.

  @param[out]     InitMmFoundationSmmArgs  Args structure

**/
STATIC
VOID
InitRiscVSmmArgs (
  IN UINTN                   ChannelId,
  OUT RPMI_SMM_MSG_CMPL_CMD  *InitMmFoundationSmmArgs
  )
{
  if (SbiMpxyInit () != EFI_SUCCESS) {
    DEBUG (
      (
       DEBUG_ERROR,
       "InitRiscVSmmArgs: "
       "Failed to init MPXY\n"
      )
      );
     InitMmFoundationSmmArgs->mm_data.Arg0       = RISCV_SMM_RET_NOT_SUPPORTED;
  } else {
     InitMmFoundationSmmArgs->mm_data.Arg0       = RISCV_SMM_RET_SUCCESS;
  }

 
  InitMmFoundationSmmArgs->mm_data.Arg1       = 0;
}

typedef struct {
  IN UINTN                    CpuId;
  IN UINTN                    MpxyChannelId;
  IN RPMI_SMM_MSG_CMPL_CMD    *SmmMessageCmd;
} RISCV_SSE_MM_CONTEXT;

/**
  SSE callback handler.

  This function is invoked when an SSE event is triggered. It handles the event
  by calling the `DelegatedEventLoop` function with the necessary context.

  @param[in] EventId    The ID of the triggered SSE event.
  @param[in] Arg        Pointer to the RISCV_SSE_MM_CONTEXT structure that contains
                        the context information for the event.

**/
STATIC
VOID
RiscVSseCallback (
  IN UINT32  EventId,
  IN VOID    *Arg
  )
{
  RISCV_SSE_MM_CONTEXT  *Context = Arg;

  DelegatedEventLoop (Context->CpuId, Context->MpxyChannelId, Context->SmmMessageCmd);
}

/**
  Initialize SSE (Secure Supervisor Execution) support.

  This function initializes the SSE context for a given CPU and MPXY channel. It
  retrieves the SSE event ID, allocates and populates a context structure, and
  registers the SSE event callback. Finally, it enables the SSE event.

  @param[in] CpuId          The ID of the CPU to initialize SSE for.
  @param[in] MpxyChannelId  The MPXY Channel ID used for communication.
  @param[in] SmmMessageCmd  Pointer to the RPMI_SMM_MSG_CMPL_CMD structure containing
                            the message command to use for SSE events.

  @retval None              This function does not return a value. It asserts on failure.
**/
STATIC
VOID
InitRiscVSse (
  IN UINTN                    CpuId,
  IN UINTN                    MpxyChannelId,
  IN RPMI_SMM_MSG_CMPL_CMD    *SmmMessageCmd
  )
{
  EFI_STATUS            Status;
  UINT32                SseEventId;
  RISCV_SSE_MM_CONTEXT  *Context;

  if (SbiMpxyReadChannelAttrs (MpxyChannelId, MpxyChanAttrSseEventId, 1, &SseEventId) != EFI_SUCCESS) {
    DEBUG (
           (
            DEBUG_ERROR,
            "InitRiscVSse: "
            "Failed to get SSE event id\n"
           )
           );
    ASSERT (0);
  }

  Context = AllocateZeroPool (sizeof (*Context));
  ASSERT (Context != NULL);

  Context->CpuId         = CpuId;
  Context->MpxyChannelId = MpxyChannelId;
  Context->SmmMessageCmd = SmmMessageCmd;
  Status = SbiSseRegisterEvent (SseEventId, (VOID *)Context, RiscVSseCallback);
  if (Status != EFI_SUCCESS) {
    DEBUG (
           (
            DEBUG_ERROR,
            "InitRiscVSse: "
            "Failed to register SSE event\n"
           )
           );
    ASSERT (0);
  }

  Status = SbiSseEnableEvent (SseEventId);
  if (Status != EFI_SUCCESS) {
    DEBUG (
           (
            DEBUG_ERROR,
            "InitRiscVSse: "
            "Failed to enable SSE event\n"
           )
           );
    ASSERT (0);
  }

  SbiSseHartUnmask();
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
  RPMI_SMM_MSG_CMPL_CMD       *InitMmFoundationSmmArgs;
  VOID                        *HobStart;

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

  InitMmFoundationSmmArgs = AllocateZeroPool (sizeof (*InitMmFoundationSmmArgs));
  ASSERT (InitMmFoundationSmmArgs != NULL);

  DEBUG ((DEBUG_INFO, "Cpu Driver EP %lx\n", PayloadBootInfo->MpxyChannelId));
  InitRiscVSmmArgs (PayloadBootInfo->MpxyChannelId, InitMmFoundationSmmArgs);
  InitRiscVSse (CpuId, PayloadBootInfo->MpxyChannelId, InitMmFoundationSmmArgs);


  //  UINTN       SmmMsgLen, SmmRespLen;
  SendMMComplete (PayloadBootInfo->MpxyChannelId, InitMmFoundationSmmArgs);
  ASSERT (0);
}
