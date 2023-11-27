/** @file
  FIT Load Image Support
Copyright (c) 2023, Intel Corporation. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiPei.h>
#include <UniversalPayload/UniversalPayload.h>
#include <UniversalPayload/DeviceTree.h>
#include <Guid/UniversalPayloadBase.h>
#include <UniversalPayload/ExtraData.h>

#include <Ppi/LoadFile.h>

#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/PeiServicesLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>

#include "FitLib.h"
#define STACK_SIZE  0x20000

CONST EFI_PEI_PPI_DESCRIPTOR  gReadyToPayloadSignalPpi = {
  (EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST),
  &gEfiReadyToPayloadPpiGuid,
  NULL
};

/**
  Notify ReadyToPayLoad signal.
  @param[in] PeiServices       An indirect pointer to the EFI_PEI_SERVICES table published by the PEI Foundation.
  @param[in] NotifyDescriptor  Address of the notification descriptor data structure.
  @param[in] Ppi               Address of the PPI that was installed.
  @retval EFI_SUCCESS          Hobs data is discovered.
  @return Others               No Hobs data is discovered.
**/
EFI_STATUS
EFIAPI
EndOfPeiPpiNotifyCallback (
  IN EFI_PEI_SERVICES           **PeiServices,
  IN EFI_PEI_NOTIFY_DESCRIPTOR  *NotifyDescriptor,
  IN VOID                       *Ppi
  )
{
  EFI_STATUS  Status;

  //
  // Ready to Payload phase signal
  //
  Status = PeiServicesInstallPpi (&gReadyToPayloadSignalPpi);

  return Status;
}

EFI_PEI_NOTIFY_DESCRIPTOR  mEndOfPeiNotifyList[] = {
  {
    (EFI_PEI_PPI_DESCRIPTOR_NOTIFY_CALLBACK | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST),
    &gEfiEndOfPeiSignalPpiGuid,
    EndOfPeiPpiNotifyCallback
  }
};

EFI_PEI_PPI_DESCRIPTOR  mEndOfPeiSignalPpi = {
  (EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST),
  &gEfiEndOfPeiSignalPpiGuid,
  NULL
};

/**
  The wrapper function of PeiLoadImageLoadImage().
  @param This            - Pointer to EFI_PEI_LOAD_FILE_PPI.
  @param FileHandle      - Pointer to the FFS file header of the image.
  @param ImageAddressArg - Pointer to PE/TE image.
  @param ImageSizeArg    - Size of PE/TE image.
  @param EntryPoint      - Pointer to entry point of specified image file for output.
  @param AuthenticationState - Pointer to attestation authentication state of image.
  @return Status of PeiLoadImageLoadImage().
**/
EFI_STATUS
EFIAPI
PeiLoadFileLoadPayload (
  IN     CONST EFI_PEI_LOAD_FILE_PPI  *This,
  IN     EFI_PEI_FILE_HANDLE          FileHandle,
  OUT    EFI_PHYSICAL_ADDRESS         *ImageAddressArg   OPTIONAL,
  OUT    UINT64                       *ImageSizeArg      OPTIONAL,
  OUT    EFI_PHYSICAL_ADDRESS         *EntryPoint,
  OUT    UINT32                       *AuthenticationState
  )
{
  EFI_STATUS                     Status;
  FIT_IMAGE_CONTEXT              Context;
  UINTN                          Instance;
  VOID                           *Binary;
  FIT_RELOCATE_ITEM              *RelocateTable;
  UNIVERSAL_PAYLOAD_BASE         *PayloadBase;
  UINTN                          Length;
  UINTN                          Delta;
  UINTN                          Index;
  VOID                           *BaseOfStack;
  VOID                           *TopOfStack;
  UNIVERSAL_PAYLOAD_DEVICE_TREE  *Fdt;
  VOID                           *Hob;

  Instance = 0;
  do {
    Status = PeiServicesFfsFindSectionData3 (EFI_SECTION_RAW, Instance++, FileHandle, &Binary, AuthenticationState);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    ZeroMem (&Context, sizeof (Context));
    Status = ParseFitImage (Binary, &Context);
  } while (EFI_ERROR (Status));

  if (EFI_ERROR (Status)) {
    ASSERT_EFI_ERROR (Status);
    return Status;
  }

  DEBUG (
    (
     DEBUG_INFO,
     "Before Rebase Payload File Base: 0x%08x, File Size: 0x%08X, EntryPoint: 0x%08x\n",
     Context.PayloadBaseAddress,
     Context.PayloadSize,
     Context.PayloadEntryPoint
    )
    );
  Context.PayloadBaseAddress = (EFI_PHYSICAL_ADDRESS)AllocatePages (EFI_SIZE_TO_PAGES (Context.PayloadSize));

  RelocateTable = (FIT_RELOCATE_ITEM *)(UINTN)(Context.PayloadBaseAddress + Context.RelocateTableOffset);
  CopyMem ((VOID *)Context.PayloadBaseAddress, Binary, Context.PayloadSize);

  if (Context.PayloadBaseAddress > Context.PayloadLoadAddress) {
    Delta                      = Context.PayloadBaseAddress - Context.PayloadLoadAddress;
    Context.PayloadEntryPoint += Delta;
    for (Index = 0; Index < Context.RelocateTableCount; Index++) {
      if ((RelocateTable[Index].RelocateType == 10) || (RelocateTable[Index].RelocateType == 3)) {
        *((UINT64 *)(Context.PayloadBaseAddress + RelocateTable[Index].Offset)) = *((UINT64 *)(Context.PayloadBaseAddress + RelocateTable[Index].Offset)) + Delta;
      }
    }
  } else {
    Delta                      = Context.PayloadLoadAddress - Context.PayloadBaseAddress;
    Context.PayloadEntryPoint -= Delta;
    for (Index = 0; Index < Context.RelocateTableCount; Index++) {
      if ((RelocateTable[Index].RelocateType == 10) || (RelocateTable[Index].RelocateType == 3)) {
        *((UINT64 *)(Context.PayloadBaseAddress + RelocateTable[Index].Offset)) = *((UINT64 *)(Context.PayloadBaseAddress + RelocateTable[Index].Offset)) - Delta;
      }
    }
  }

  DEBUG (
    (
     DEBUG_INFO,
     "After Rebase Payload File Base: 0x%08x, File Size: 0x%08X, EntryPoint: 0x%08x\n",
     Context.PayloadBaseAddress,
     Context.PayloadSize,
     Context.PayloadEntryPoint
    )
    );

  Length      = sizeof (UNIVERSAL_PAYLOAD_BASE);
  PayloadBase = BuildGuidHob (
                  &gUniversalPayloadBaseGuid,
                  Length
                  );
  PayloadBase->Entry = (EFI_PHYSICAL_ADDRESS)Context.ImageBase;

  *ImageAddressArg = Context.PayloadBaseAddress;
  *ImageSizeArg    = Context.PayloadSize;
  *EntryPoint      = Context.PayloadEntryPoint;

  Status = PeiServicesNotifyPpi (&mEndOfPeiNotifyList[0]);
  ASSERT_EFI_ERROR (Status);

  Status = PeiServicesInstallPpi (&mEndOfPeiSignalPpi);
  ASSERT_EFI_ERROR (Status);

  Hob = GetFirstGuidHob (&gUniversalPayloadDeviceTreeGuid);
  if (Hob != NULL) {
    Fdt =  (UNIVERSAL_PAYLOAD_DEVICE_TREE *)GET_GUID_HOB_DATA (Hob);
  }

  //
  // Allocate 128KB for the Stack
  //
  BaseOfStack = AllocatePages (EFI_SIZE_TO_PAGES (STACK_SIZE));
  ASSERT (BaseOfStack != NULL);

  //
  // Compute the top of the stack we were allocated. Pre-allocate a UINTN
  // for safety.
  //
  TopOfStack = (VOID *)((UINTN)BaseOfStack + EFI_SIZE_TO_PAGES (STACK_SIZE) * EFI_PAGE_SIZE - CPU_STACK_ALIGNMENT);
  TopOfStack = ALIGN_POINTER (TopOfStack, CPU_STACK_ALIGNMENT);

  //
  // Transfer the control to the entry point of UniveralPayloadEntry.
  //
  SwitchStack (
    (SWITCH_STACK_ENTRY_POINT)(UINTN)Context.PayloadEntryPoint,
    (VOID *)(Fdt->DeviceTreeAddress),
    NULL,
    TopOfStack
    );

  return EFI_SUCCESS;
}

EFI_PEI_LOAD_FILE_PPI  mPeiLoadFilePpi = {
  PeiLoadFileLoadPayload
};

EFI_PEI_PPI_DESCRIPTOR  gPpiLoadFilePpiList = {
  (EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST),
  &gEfiPeiLoadFilePpiGuid,
  &mPeiLoadFilePpi
};

/**
  Install Pei Load File PPI.
  @param  FileHandle  Handle of the file being invoked.
  @param  PeiServices Describes the list of possible PEI Services.
  @retval EFI_SUCESS  The entry point executes successfully.
  @retval Others      Some error occurs during the execution of this function.
**/
EFI_STATUS
EFIAPI
InitializeFitPayloadLoaderPeim (
  IN       EFI_PEI_FILE_HANDLE  FileHandle,
  IN CONST EFI_PEI_SERVICES     **PeiServices
  )
{
  EFI_STATUS  Status;

  Status = PeiServicesInstallPpi (&gPpiLoadFilePpiList);

  return Status;
}
