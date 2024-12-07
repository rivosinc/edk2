/** @file
  This module provides communication with RAS Agent over RPMI/MPXY

  Copyright (c) 2024, Ventana Micro Systems, Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Base.h>
#include <Uefi.h>

#include <IndustryStandard/Acpi.h>

#include <Protocol/AcpiTable.h>

#include <Guid/EventGroup.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/FdtClient.h>
#include <Protocol/MmCommunication2.h>

#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/SafeIntLib.h>
#include <Library/BaseRiscVSbiLib.h>

#include <Library/DxeRiscvMpxy.h>
#include <Library/DxeRiscvRasAgentClient.h>

#define MAX_SOURCES    512
#define MAX_DESC_SIZE  1024

///
/// Size of SMM communicate header, without including the payload.
///
#define MM_COMMUNICATE_HEADER_SIZE  (OFFSET_OF (EFI_MM_COMMUNICATE_HEADER, Data))

/* RAS Agent Services on MPXY/RPMI */
#define RAS_GET_NUM_ERR_SRCS      0x1
#define RAS_GET_ERR_SRCS_ID_LIST  0x2
#define RAS_GET_ERR_SRC_DESC      0x3

#define __packed32  __attribute__((packed,aligned(__alignof__(UINT32))))
EFI_MM_COMMUNICATION2_PROTOCOL  *mMmCommunication2 = NULL;

typedef struct __packed32 {
  UINT32    status;
  UINT32    flags;
  UINT32    remaining;
  UINT32    returned;
  UINT32    func_id;
} RasRpmiRespHeader;

typedef struct __packed32 {
  RasRpmiRespHeader    RespHdr;
  UINT32               ErrSourceList[MAX_SOURCES];
} ErrorSourceListResp;

typedef struct __packed32 {
  RasRpmiRespHeader    RspHdr;
  UINT8                desc[MAX_DESC_SIZE];
} ErrDescResp;

static ErrorSourceListResp  gErrorSourceListResp;
static ErrDescResp          gErrDescResp;
UINT32                      gMpxyChannelId = 0;

#define MPXY_SHMEM_SIZE  4096

#if CHANNEL_FROM_FDT
STATIC
EFI_STATUS
EFIAPI
GetRasAgentMpxyChannelIdFromFDT (
  OUT UINT32  *ChannelId
  )
{
  EFI_STATUS           Status;
  FDT_CLIENT_PROTOCOL  *FdtClient;
  INT32                Node;
  CONST UINT64         *Reg;
  UINT32               RegSize;
  UINT32               RegBase;

  Status = gBS->LocateProtocol (
                  &gFdtClientProtocolGuid,
                  NULL,
                  (VOID **)&FdtClient
                  );

  ASSERT_EFI_ERROR (Status);

  Status = FdtClient->FindCompatibleNode (FdtClient, "riscv,sbi-mpxy-ras-agent", &Node);

  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_WARN,
      "%a: No 'riscv,sbi-mpxy-ras-agent' compatible DT node found\n",
      __func__
      ));
    return EFI_NOT_FOUND;
  }

  Status = FdtClient->GetNodeProperty (
                        FdtClient,
                        Node,
                        "riscv,sbi-mpxy-channel-id",
                        (CONST VOID **)&Reg,
                        &RegSize
                        );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_WARN,
      "%a: No 'riscv,sbi-mpxy-channel-id' compatible DT node found\n",
      __func__
      ));
    return EFI_NOT_FOUND;
  }

  ASSERT (RegSize == 4);

  RegBase = SwapBytes32 (Reg[0]);

  *ChannelId = RegBase;

  return EFI_SUCCESS;
}

#endif

STATIC
EFI_STATUS
EFIAPI
ProbeRasAgentMpxyChannelId (
  OUT UINT32  *ChannelId
  )
{
  #define MAX_MPXY_CHANNELS  64
  UINTN       ChannelList[MAX_MPXY_CHANNELS];
  UINTN       Returned, Remaining, StartIndex = 0;
  EFI_STATUS  Status;
  BOOLEAN     Found = FALSE, ParsingDone = FALSE;
  UINTN       i, Id;
  UINT32      RasSrvGroup;

  while (!ParsingDone) {
    Status = SbiMpxyGetChannelList (
               StartIndex, /* Start index */
               &ChannelList[0],
               &Remaining,
               &Returned
               );

    if (Status != EFI_SUCCESS) {
      return Status;
    }

    /* This read has returned zero and we still haven't got what we need */
    if (Returned == 0) {
      return EFI_UNSUPPORTED;
    }

    for (i = 0; i < Returned; i++) {
      Id     = ChannelList[0];
      Status = SbiMpxyReadChannelAttrs (
                 Id,
                 0x80000000, /* Base attribute Id */
                 1,          /* Number of attributes to be read */
                 &RasSrvGroup
                 );

      if (Status != EFI_SUCCESS) {
        continue;
      }

      if (RasSrvGroup == 0xB) {
        Found       = TRUE;
        ParsingDone = TRUE;
        break;
      }
    }

    /* Read if some more to be read else we are done parsing */
    if (Remaining) {
      StartIndex = Returned;
      continue;
    } else {
      ParsingDone = TRUE;
    }
  }

  if (Found == TRUE) {
    *ChannelId = Id;
    DEBUG ((
      DEBUG_INFO,
      "Found RAS MPXY channel: %x\n",
      Id
      ));
  }

  return Status;
}

// Global function pointer
EFI_STATUS (EFIAPI *gSendCommand)(VOID *CommBuffer, UINTN *RespLen, UINT8 FuncId);

// Implementation of RacSendMMCommand
EFI_STATUS EFIAPI
RacSendMMCommand (
  VOID   *CommBuffer,
  UINTN  *RespLen,
  UINT8  FuncId
  )
{
  EFI_STATUS                 Status;
  UINTN                      CommBufferSize;
  EFI_MM_COMMUNICATE_HEADER  *SmmCommunicateHeader;

  CommBufferSize       = MM_COMMUNICATE_HEADER_SIZE + *RespLen;
  SmmCommunicateHeader = AllocateZeroPool (CommBufferSize);
  if (SmmCommunicateHeader == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  CopyGuid (&SmmCommunicateHeader->HeaderGuid, &gMmHestGetErrorSourceInfoGuid);
  CopyMem (SmmCommunicateHeader->Data, (const void *)CommBuffer, *RespLen);
  SmmCommunicateHeader->MessageLength = *RespLen;

  Status = mMmCommunication2->Communicate (
                                mMmCommunication2,
                                SmmCommunicateHeader,
                                SmmCommunicateHeader,
                                &CommBufferSize
                                );

  *RespLen = CommBufferSize - MM_COMMUNICATE_HEADER_SIZE;
  CopyMem (CommBuffer, (const void *)SmmCommunicateHeader->Data, *RespLen);
  FreePool (SmmCommunicateHeader);

  return Status;
}

// Implementation of RacSendPassThroughCommand
EFI_STATUS EFIAPI
RacSendPassThroughCommand (
  VOID   *CommBuffer,
  UINTN  *RespLen,
  UINT8  FuncId
  )
{
  EFI_STATUS  Status;

  Status = SbiMpxySendMessage (
             gMpxyChannelId,
             FuncId,
             CommBuffer,
             sizeof (UINT32),
             CommBuffer,
             RespLen
             );

  return Status;
}

EFI_STATUS
EFIAPI
GetRasAgentMpxyChannelId (
  OUT UINT32  *ChannelId
  )
{
 #if CHANNEL_FROM_FDT
  return GetRasAgentMpxyChannelIdFromFDT (ChannelId);
 #else
  return ProbeRasAgentMpxyChannelId (ChannelId);
 #endif
}

EFI_STATUS
EFIAPI
RacInit (
  VOID
  )
{
  EFI_STATUS  Status;

  if (PcdGetBool (PcdMMPassThroughEnable) == FALSE) {
    Status = gBS->LocateProtocol (&gEfiMmCommunication2ProtocolGuid, NULL, (VOID **)&mMmCommunication2);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    gSendCommand = RacSendMMCommand;
  } else {
    if (GetRasAgentMpxyChannelId (&gMpxyChannelId) != EFI_SUCCESS) {
      return EFI_NOT_READY;
    }

    if (SbiMpxyChannelOpen (gMpxyChannelId) != EFI_SUCCESS) {
      return EFI_NOT_READY;
    }

    gSendCommand = RacSendPassThroughCommand;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
RacGetNumberErrorSources (
  UINT32  *NumErrorSources
  )
{
  struct __packed32 _NumErrSrc {
    RasRpmiRespHeader    RespHdr;
    UINT32               NumErrorSources;
  } RasMsgBuf;

  EFI_STATUS         Status;
  RasRpmiRespHeader  *RespHdr = &RasMsgBuf.RespHdr;
  UINTN              RespLen  = sizeof (RasMsgBuf);

  ZeroMem (&RasMsgBuf, sizeof (RasMsgBuf));
  RespHdr->func_id = RAS_GET_NUM_ERR_SRCS;

  Status = gSendCommand (&RasMsgBuf, &RespLen, RAS_GET_NUM_ERR_SRCS);
  if (Status != EFI_SUCCESS) {
    return Status;
  }

  if (RespHdr->status != 0) {
    return EFI_DEVICE_ERROR;
  }

  *NumErrorSources = RasMsgBuf.NumErrorSources;

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
RacGetErrorSourceIDList (
  OUT UINT32  **ErrorSourceList,
  OUT UINT32  *NumSources
  )
{
  UINT32             *RespData = &gErrorSourceListResp.ErrSourceList[0];
  RasRpmiRespHeader  *RespHdr  = &gErrorSourceListResp.RespHdr;
  EFI_STATUS         Status;
  UINTN              RespLen = sizeof (gErrorSourceListResp);

  ZeroMem (&gErrorSourceListResp, sizeof (gErrorSourceListResp));

  if (!ErrorSourceList) {
    return EFI_INVALID_PARAMETER;
  }

  gErrorSourceListResp.RespHdr.func_id = RAS_GET_ERR_SRCS_ID_LIST;
  Status                               = gSendCommand (&gErrorSourceListResp, &RespLen, RAS_GET_ERR_SRCS_ID_LIST);
  if (Status != EFI_SUCCESS) {
    return Status;
  }

  if (RespHdr->status != 0) {
    return EFI_DEVICE_ERROR;
  }

  *NumSources      = RespHdr->returned;
  *ErrorSourceList = RespData;

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
RacGetErrorSourceDescriptor (
  IN UINT32   SourceID,
  OUT UINTN   *DescriptorType,
  OUT VOID    **ErrorDescriptor,
  OUT UINT32  *ErrorDescriptorSize
  )
{
  UINTN              RespLen = sizeof (gErrDescResp);
  EFI_STATUS         Status;
  RasRpmiRespHeader  *RspHdr = &gErrDescResp.RspHdr;
  UINT8              *desc   = &gErrDescResp.desc[0];

  ZeroMem (&gErrDescResp, sizeof (gErrDescResp));

  *desc                       = SourceID;
  gErrDescResp.RspHdr.func_id = RAS_GET_ERR_SRC_DESC;
  Status                      = gSendCommand (&gErrDescResp, &RespLen, RAS_GET_ERR_SRC_DESC);
  if (Status != EFI_SUCCESS) {
    return Status;
  }

  if (RspHdr->status != 0) {
    return EFI_DEVICE_ERROR;
  }

  if (RspHdr->remaining != 0) {
    return EFI_DEVICE_ERROR;
  }

  *DescriptorType = RspHdr->flags & ERROR_DESCRIPTOR_TYPE_MASK;

  ASSERT (*DescriptorType < MAX_ERROR_DESCRIPTOR_TYPES);

  *ErrorDescriptor     = (VOID *)desc;
  *ErrorDescriptorSize = RspHdr->returned;

  return EFI_SUCCESS;
}
