/** @file

  Copyright (c) 2016-2021, Arm Limited.
  Copyright (c) 2023, Intel Corporation, All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Library/BaseLib.h>
#include "Library/BaseRiscVSbiLib.h"
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/HobLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/DxeRiscvMpxy.h>

#include <Protocol/MmCommunication2.h>
#include <Protocol/FdtClient.h>

#include "MmCommunicate.h"

//
// Address, Length of the pre-allocated buffer for communication with the secure
// world.
//
STATIC RISCV_SMM_MEM_REGION_DESCRIPTOR  mNsCommBuffMemRegion;

//
// Handle to install the MM Communication Protocol
//
STATIC EFI_HANDLE  mMmCommunicateHandle;

//
// The MM Channel ID, the value should be loaded from device tree
//
STATIC UINT32  mMmChannelId = 0;

/**
  Communicates with a registered handler.

  This function provides a service to send and receive messages from a registered UEFI service.

  @param[in] This                     The EFI_MM_COMMUNICATION_PROTOCOL instance.
  @param[in, out] CommBufferPhysical  Physical address of the MM communication buffer
  @param[in, out] CommBufferVirtual   Virtual address of the MM communication buffer
  @param[in, out] CommSize            The size of the data buffer being passed in. On input,
                                      when not omitted, the buffer should cover EFI_MM_COMMUNICATE_HEADER
                                      and the value of MessageLength field. On exit, the size
                                      of data being returned. Zero if the handler does not
                                      wish to reply with any data. This parameter is optional
                                      and may be NULL.

  @retval EFI_SUCCESS            The message was successfully posted.
  @retval EFI_INVALID_PARAMETER  CommBufferPhysical or CommBufferVirtual was NULL, or
                                 integer value pointed by CommSize does not cover
                                 EFI_MM_COMMUNICATE_HEADER and the value of MessageLength
                                 field.
  @retval EFI_BAD_BUFFER_SIZE    The buffer is too large for the MM implementation.
                                 If this error is returned, the MessageLength field
                                 in the CommBuffer header or the integer pointed by
                                 CommSize, are updated to reflect the maximum payload
                                 size the implementation can accommodate.
  @retval EFI_ACCESS_DENIED      The CommunicateBuffer parameter or CommSize parameter,
                                 if not omitted, are in address range that cannot be
                                 accessed by the MM environment.

**/
EFI_STATUS
EFIAPI
MmCommunication2Communicate (
  IN CONST EFI_MM_COMMUNICATION2_PROTOCOL  *This,
  IN OUT VOID                              *CommBufferPhysical,
  IN OUT VOID                              *CommBufferVirtual,
  IN OUT UINTN                             *CommSize OPTIONAL
  )
{
  EFI_MM_COMMUNICATE_HEADER  *CommunicateHeader;
  RISCV_SMM_MSG_COMM_ARGS    CommunicateArgs;
  EFI_STATUS                 Status;
  UINTN                      BufferSize;
  UINTN                      MmRespLen;

  Status     = EFI_ACCESS_DENIED;
  BufferSize = 0;

  ZeroMem (&CommunicateArgs, sizeof (RISCV_SMM_MSG_COMM_ARGS));

  //
  // Check parameters
  //
  if ((CommBufferVirtual == NULL) || (CommBufferPhysical == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Status            = EFI_SUCCESS;
  CommunicateHeader = CommBufferVirtual;
  // CommBuffer is a mandatory parameter. Hence, Rely on
  // MessageLength + Header to ascertain the
  // total size of the communication payload rather than
  // rely on optional CommSize parameter
  BufferSize = CommunicateHeader->MessageLength +
               sizeof (CommunicateHeader->HeaderGuid) +
               sizeof (CommunicateHeader->MessageLength);

  // If CommSize is not omitted, perform size inspection before proceeding.
  if (CommSize != NULL) {
    // This case can be used by the consumer of this driver to find out the
    // max size that can be used for allocating CommBuffer.
    if ((*CommSize == 0) ||
        (*CommSize > mNsCommBuffMemRegion.Length))
    {
      *CommSize = mNsCommBuffMemRegion.Length;
      Status    = EFI_BAD_BUFFER_SIZE;
    }

    //
    // CommSize should cover at least MessageLength + sizeof (EFI_MM_COMMUNICATE_HEADER);
    //
    if (*CommSize < BufferSize) {
      Status = EFI_INVALID_PARAMETER;
    }
  }

  //
  // If the message length is 0 or greater than what can be tolerated by the MM
  // environment then return the expected size.
  //
  if ((CommunicateHeader->MessageLength == 0) ||
      (BufferSize > mNsCommBuffMemRegion.Length))
  {
    CommunicateHeader->MessageLength = mNsCommBuffMemRegion.Length -
                                       sizeof (CommunicateHeader->HeaderGuid) -
                                       sizeof (CommunicateHeader->MessageLength);
    Status = EFI_BAD_BUFFER_SIZE;
  }

  // MessageLength or CommSize check has failed, return here.
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Copy Communication Payload
  CopyMem ((VOID *)mNsCommBuffMemRegion.VirtualBase, CommBufferVirtual, BufferSize);

  // comm_buffer_address (64-bit physical address)
  CommunicateArgs.Arg0 = (UINTN)mNsCommBuffMemRegion.PhysicalBase;

  // comm_size_address (not used, indicated by setting to zero)
  CommunicateArgs.Arg1 = 0;

  // Call the Standalone MM environment.
  Status = SbiMpxySendMessage (
             mMmChannelId,
             RISCV_MSG_ID_SMM_COMMUNICATE,
             (VOID *)&CommunicateArgs,
             sizeof (RISCV_SMM_MSG_COMM_ARGS),
             (VOID *)&CommunicateArgs,
             &MmRespLen
             );
  if (EFI_ERROR (Status) || (0 == MmRespLen)) {
    return Status;
  }

  switch (CommunicateArgs.Arg0) {
    case RISCV_SMM_RET_SUCCESS:
      ZeroMem (CommBufferVirtual, BufferSize);
      // On successful return, the size of data being returned is inferred from
      // MessageLength + Header.
      CommunicateHeader = (EFI_MM_COMMUNICATE_HEADER *)mNsCommBuffMemRegion.VirtualBase;
      BufferSize        = CommunicateHeader->MessageLength +
                          sizeof (CommunicateHeader->HeaderGuid) +
                          sizeof (CommunicateHeader->MessageLength);

      CopyMem (
        CommBufferVirtual,
        (VOID *)mNsCommBuffMemRegion.VirtualBase,
        BufferSize
        );
      Status = EFI_SUCCESS;
      break;

    case RISCV_SMM_RET_INVALID_PARAMS:
      Status = EFI_INVALID_PARAMETER;
      break;

    case RISCV_SMM_RET_DENIED:
      Status = EFI_ACCESS_DENIED;
      break;

    case RISCV_SMM_RET_NO_MEMORY:
      // Unexpected error since the CommSize was checked for zero length
      // prior to issuing the SMC
      Status = EFI_OUT_OF_RESOURCES;
      ASSERT (0);
      break;

    default:
      Status = EFI_ACCESS_DENIED;
      ASSERT (0);
  }

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
GetMmMpxyChannelId (
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

  Status = FdtClient->FindCompatibleNode (FdtClient, "riscv,sbi-mpxy-mm", &Node);
  if (EFI_ERROR (Status)) {
    DEBUG (
      (
       DEBUG_WARN,
       "%a: No 'riscv,sbi-mpxy-mm' compatible DT node found\n",
       __func__
      )
      );
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
    DEBUG (
      (
       DEBUG_WARN,
       "%a: No 'riscv,sbi-mpxy-channel-id' compatible DT node found\n",
       __func__
      )
      );
    return EFI_NOT_FOUND;
  }

  ASSERT (RegSize == 4);

  RegBase = SwapBytes32 (Reg[0]);

  *ChannelId = RegBase;

  return EFI_SUCCESS;
}

//
// MM Communication Protocol instance
//
STATIC EFI_MM_COMMUNICATION2_PROTOCOL  mMmCommunication2 = {
  MmCommunication2Communicate
};

STATIC
EFI_STATUS
GetMmCompatibility (
  )
{
  EFI_STATUS               Status;
  UINT32                   MmVersion;
  RISCV_SMM_MSG_COMM_ARGS  MmVersionArgs;
  UINTN                    MmRespLen;

  Status = SbiMpxySendMessage (
             mMmChannelId,
             RISCV_MSG_ID_SMM_VERSION,
             (VOID *)&MmVersionArgs,
             sizeof (RISCV_SMM_MSG_COMM_ARGS),
             (VOID *)&MmVersionArgs,
             &MmRespLen
             );

  if (EFI_ERROR (Status) || (0 == MmRespLen)) {
    return Status;
  }

  MmVersion = MmVersionArgs.Arg0;

  if ((MM_MAJOR_VER (MmVersion) == MM_CALLER_MAJOR_VER) &&
      (MM_MINOR_VER (MmVersion) >= MM_CALLER_MINOR_VER))
  {
    DEBUG (
      (
       DEBUG_INFO,
       "MM Version: Major=0x%x, Minor=0x%x\n",
       MM_MAJOR_VER (MmVersion),
       MM_MINOR_VER (MmVersion)
      )
      );
    Status = EFI_SUCCESS;
  } else {
    DEBUG (
      (
       DEBUG_ERROR,
       "Incompatible MM Versions.\n Current Version: Major=0x%x, Minor=0x%x.\n Expected: Major=0x%x, Minor>=0x%x.\n",
       MM_MAJOR_VER (MmVersion),
       MM_MINOR_VER (MmVersion),
       MM_CALLER_MAJOR_VER,
       MM_CALLER_MINOR_VER
      )
      );
    Status = EFI_UNSUPPORTED;
  }

  return Status;
}

STATIC EFI_GUID *CONST  mGuidedEventGuid[] = {
  &gEfiEndOfDxeEventGroupGuid,
  &gEfiEventExitBootServicesGuid,
  &gEfiEventReadyToBootGuid,
};

STATIC EFI_EVENT  mGuidedEvent[ARRAY_SIZE (mGuidedEventGuid)];

/**
  Event notification that is fired when GUIDed Event Group is signaled.

  @param  Event                 The Event that is being processed, not used.
  @param  Context               Event Context, not used.

**/
STATIC
VOID
EFIAPI
MmGuidedEventNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_MM_COMMUNICATE_HEADER  Header;
  UINTN                      Size;

  //
  // Use Guid to initialize EFI_SMM_COMMUNICATE_HEADER structure
  //
  CopyGuid (&Header.HeaderGuid, Context);
  Header.MessageLength = 1;
  Header.Data[0]       = 0;

  Size = sizeof (Header);
  MmCommunication2Communicate (&mMmCommunication2, &Header, &Header, &Size);
}

/**
  The Entry Point for MM Communication

  This function installs the MM communication protocol interface and finds out
  what type of buffer management will be required prior to invoking the
  communication SMC.

  @param  ImageHandle    The firmware allocated handle for the EFI image.
  @param  SystemTable    A pointer to the EFI System Table.

  @retval EFI_SUCCESS    The entry point is executed successfully.
  @retval Other          Some error occurred when executing this entry point.

**/
EFI_STATUS
EFIAPI
MmCommunication2Initialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  UINTN       Index;

 #if 0
  VOID    *SbiShmem;
  UINT64  ShmemP;
 #endif

  // Check if the Mpxy channel exist
  Status = GetMmMpxyChannelId (&mMmChannelId);
  if (EFI_ERROR (Status)) {
    goto ReturnErrorStatus;
  }

  if (SbiMpxyChannelOpen (mMmChannelId) != EFI_SUCCESS) {
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
  if (FALSE == SbiMpxyShmemInitialized ()) {
    //
    // Allocate memory to be shared with OpenSBI for MPXY
    //
    SbiShmem = AllocateAlignedPages (
                 EFI_SIZE_TO_PAGES (MPXY_SHMEM_SIZE),
                 MPXY_SHMEM_SIZE                     // Align
                 );

    if (SbiShmem == NULL) {
      goto ReturnErrorStatus;
    }

    ZeroMem (SbiShmem, MPXY_SHMEM_SIZE);
    ShmemP = (UINT64)(SbiShmem);
    Status = SbiMpxySetShmem (
               0UL,
               ShmemP,
               MPXY_SHMEM_SIZE
               );
    if (EFI_ERROR (Status)) {
      goto ReturnErrorStatus;
    }
  }

 #endif

  // Check if we can make the MM call
  Status = GetMmCompatibility ();
  if (EFI_ERROR (Status)) {
    goto ReturnErrorStatus;
  }

  mNsCommBuffMemRegion.PhysicalBase = PcdGet64 (PcdMmBufferBase);
  // During boot , Virtual and Physical are same
  mNsCommBuffMemRegion.VirtualBase = mNsCommBuffMemRegion.PhysicalBase;
  mNsCommBuffMemRegion.Length      = PcdGet64 (PcdMmBufferSize);

  ASSERT (mNsCommBuffMemRegion.PhysicalBase != 0);

  ASSERT (mNsCommBuffMemRegion.Length != 0);

  // Install the communication protocol
  Status = gBS->InstallProtocolInterface (
                  &mMmCommunicateHandle,
                  &gEfiMmCommunication2ProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mMmCommunication2
                  );
  if (EFI_ERROR (Status)) {
    DEBUG (
      (
       DEBUG_ERROR,
       "MmCommunicationInitialize: "
       "Failed to install MM communication protocol\n"
      )
      );
    goto CleanAddedMemorySpace;
  }

  for (Index = 0; Index < ARRAY_SIZE (mGuidedEventGuid); Index++) {
    Status = gBS->CreateEventEx (
                    EVT_NOTIFY_SIGNAL,
                    TPL_CALLBACK,
                    MmGuidedEventNotify,
                    mGuidedEventGuid[Index],
                    mGuidedEventGuid[Index],
                    &mGuidedEvent[Index]
                    );
    ASSERT_EFI_ERROR (Status);
    if (EFI_ERROR (Status)) {
      while (Index-- > 0) {
        gBS->CloseEvent (mGuidedEvent[Index]);
      }

      goto UninstallProtocol;
    }
  }

  return EFI_SUCCESS;

UninstallProtocol:
  gBS->UninstallProtocolInterface (
         mMmCommunicateHandle,
         &gEfiMmCommunication2ProtocolGuid,
         &mMmCommunication2
         );

CleanAddedMemorySpace:
  gDS->RemoveMemorySpace (
         mNsCommBuffMemRegion.PhysicalBase,
         mNsCommBuffMemRegion.Length
         );

ReturnErrorStatus:
  return EFI_INVALID_PARAMETER;
}
