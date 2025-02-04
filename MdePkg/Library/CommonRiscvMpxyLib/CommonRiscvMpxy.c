/** @file
  This module implements functions to be used by MPXY client

  Copyright (c) 2024, Ventana Micro Systems, Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Base.h>
#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/SafeIntLib.h>
#include <Library/BaseRiscVSbiLib.h>
#include <Library/DxeRiscvMpxy.h>

#define INVAL_PHYS_ADDR  (-1U)
#define INVALID_CHAN     -1

#define MPXY_SHMEM_SIZE  4096

#if defined (__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__  /* CPU(little-endian) */
#define LLE_TO_CPU(x)  (SwapBytes64(x))
#define CPU_TO_LLE(x)  (SwapBytes64(x))
#else
#define LLE_TO_CPU(x)  (x)
#define CPU_TO_LLE(x)  (x)
#endif

STATIC VOID     *gShmemVirt         = NULL;
STATIC UINTN    gNrShmemPages       = 0;
STATIC UINT64   gShmemPhysHi        = INVAL_PHYS_ADDR;
STATIC UINT64   gShmemPhysLo        = INVAL_PHYS_ADDR;
STATIC UINT64   gShmemSize          = 0;
STATIC BOOLEAN  gMpxyLibInitialized = FALSE;
STATIC UINT64   gShmemSet           = 0;

STATIC
EFI_STATUS
EFIAPI
SbiMpxySetShmem (
  IN UINT64   ShmemPhysHi,
  IN UINT64   ShmemPhysLo
  )
{
  SBI_RET  Ret;

  Ret = SbiCall (
          SBI_EXT_MPXY,
          SBI_EXT_MPXY_SET_SHMEM,
          3,
          CPU_TO_LLE (ShmemPhysLo),
          CPU_TO_LLE (ShmemPhysHi),
          0
          );

  if (ShmemPhysLo == INVAL_PHYS_ADDR) {
    gShmemSet = 0;
  } else {
    gShmemSet = 1;
  }

  gShmemPhysLo = ShmemPhysLo;
  gShmemPhysHi = ShmemPhysHi;

  return TranslateError (Ret.Error);
}

STATIC
EFI_STATUS
EFIAPI
SbiMpxyDisableShmem (
  VOID
  )
{
  EFI_STATUS  Status;

  if (!gShmemSet) {
    return EFI_SUCCESS;
  }

  Status = SbiMpxySetShmem (
             INVAL_PHYS_ADDR,
             INVAL_PHYS_ADDR
             );

  return Status;
}

BOOLEAN
SbiMpxyShmemInitialized (
  VOID
  )
{
  return (gMpxyLibInitialized);
}

EFI_STATUS
EFIAPI
SbiMpxyGetChannelList (
  IN  UINTN  StartIndex,
  OUT UINTN  *ChannelList,
  OUT UINTN  *Remaining,
  OUT UINTN  *Returned
  )
{
  SBI_RET     Ret;
  UINT32      *Shmem = gShmemVirt;
  UINTN       i;

  if (!gMpxyLibInitialized) {
    return (EFI_DEVICE_ERROR);
  }

  Ret = SbiCall (
          SBI_EXT_MPXY,
          SBI_EXT_MPXY_GET_CHANNEL_IDS,
          1,
          StartIndex
          );

  if (Ret.Error != SBI_SUCCESS) {
    return TranslateError (Ret.Error);
  }

  /* Index 0 contains number of channels pending to be read */
  if (Shmem[0] == 0) {
    *Remaining = 0;
  }

  /* Number of channels returned */
  if (Shmem[1] > 0) {
    for (i = 0; i < Shmem[1]; i++) {
      ChannelList[i] = Shmem[i+2];
    }
  }

  *Returned = Shmem[1];

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SbiMpxyReadChannelAttrs (
  IN UINTN    ChannelId,
  IN UINT32   BaseAttrId,
  IN UINT32   NrAttrs,
  OUT UINT32  *Attrs
  )
{
  SBI_RET     Ret;

  if (!gMpxyLibInitialized) {
    return (EFI_DEVICE_ERROR);
  }

  Ret = SbiCall (
          SBI_EXT_MPXY,
          SBI_EXT_MPXY_READ_ATTRS,
          3,
          ChannelId,
          BaseAttrId, /* Base attribute Id */
          NrAttrs     /* Number of attributes */
          );

  if (Ret.Error != SBI_SUCCESS) {
    return TranslateError (Ret.Error);
  }

  CopyMem (
    Attrs,
    gShmemVirt,
    NrAttrs * sizeof(UINT32)
    );

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SbiMpxyInit (
  VOID
  )
{
  VOID        *SbiShmem;
  UINTN       NrEfiPages;
  EFI_STATUS  Status;
  SBI_RET     Ret;

  if (SbiMpxyShmemInitialized() == TRUE) {
	  return EFI_SUCCESS;
  }

  Status = SbiProbeExtension (SBI_EXT_MPXY);
  ASSERT_EFI_ERROR (Status);

//   Status = SbiMpxyReadChannelAttrs (
//              ChannelId,
//              0,
//              MpxyChanAttrMax,
//              &Attributes[0]
//              );

//   if (EFI_ERROR (Status)) {
//     return Status;
//   }

//   ChanDataLen = Attributes[MpxyChanAttrMsgDataMaxLen];
//   NrEfiPages  = EFI_SIZE_TO_PAGES (ChanDataLen);
  Ret = SbiCall (
          SBI_EXT_MPXY,
          SBI_EXT_MPXY_GET_SHMEM_SIZE,
          0
          );
  if (Ret.Error != SBI_SUCCESS) {
    return TranslateError (Ret.Error);
  }

  NrEfiPages = (Ret.Value / EFI_PAGE_SIZE) + 1;
  gShmemSize = Ret.Value;

  /* No shared memory yet. Allocate a new one. */
  SbiShmem = AllocateAlignedPages (
                NrEfiPages,
                EFI_PAGE_SIZE
                );

  if (SbiShmem == NULL) {
    return (EFI_OUT_OF_RESOURCES);
  }

  Status = SbiMpxySetShmem (
              0,
              (UINT64)SbiShmem
              );

  if (EFI_ERROR (Status)) {
    FreeAlignedPages (SbiShmem, NrEfiPages);
    return (EFI_DEVICE_ERROR);
  }

  /* Save the new shared memory */
  gShmemVirt    = SbiShmem;
  gNrShmemPages = NrEfiPages;

  gMpxyLibInitialized = TRUE;

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SbiMpxyDeinit (
  VOID
  )
{
  EFI_STATUS  Status;

  if (SbiMpxyShmemInitialized() == FALSE) {
    return (EFI_INVALID_PARAMETER);
  }

  /* Ref count is zero. Release the memory */
  Status = SbiMpxyDisableShmem ();
  if (EFI_ERROR (Status)) {
    return (EFI_DEVICE_ERROR);
  }

  FreeAlignedPages (gShmemVirt, gNrShmemPages);

  return (EFI_SUCCESS);
}

EFI_STATUS
EFIAPI
SbiMpxySendMessage (
  IN UINTN   ChannelId,
  IN UINTN   MessageId,
  IN VOID    *Message,
  IN UINTN   MessageDataLen,
  OUT VOID   *Response,
  OUT UINTN  *ResponseLen
  )
{
  SBI_RET  Ret;
  UINT64   Phys = gShmemPhysLo;

  if (SbiMpxyShmemInitialized() == FALSE) {
    return EFI_INVALID_PARAMETER;
  }

  if (MessageDataLen >= gShmemSize) {
    return EFI_INVALID_PARAMETER;
  }

  /* Copy message to Hart's shared memory */
  CopyMem (
    (VOID *)Phys,
    Message,
    MessageDataLen
    );

  Ret = SbiCall (
          SBI_EXT_MPXY,
          SBI_EXT_MPXY_SEND_MSG_WITH_RESP,
          3,
          ChannelId,
          MessageId,
          MessageDataLen
          );

  if ((Ret.Error == SBI_SUCCESS) && Ret.Value) {
    /* Copy the response to out buffer */
    CopyMem (
      Response,
      (const VOID *)Phys,
      Ret.Value
      );
    if (ResponseLen) {
      *ResponseLen = Ret.Value;
    }
  }

  return TranslateError (Ret.Error);
}
