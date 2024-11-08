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

enum {
  MpxyChanAttrProtId,
  MpxyChanAttrProtVersion,
  MpxyChanAttrMsgDataMaxLen,
  MpxyChanAttrMsgSendTimeout,
  MpxyChanAttrCapability,
  MpxyChanAttrMsiAddrLow,
  MpxyChanAttrMsiAddrHigh,
  MpxyChanAttrMsiData,
  MpxyChanAttrSseEventId,
  MpxyChanAttrEventStateControl,
  MpxyChanAttrMax
};

STATIC VOID     *gNonChanTempShmem  = NULL;
STATIC VOID     *gShmemVirt         = NULL;
STATIC UINTN    gNrShmemPages       = 0;
STATIC UINT64   gShmemPhysHi        = INVAL_PHYS_ADDR;
STATIC UINT64   gShmemPhysLo        = INVAL_PHYS_ADDR;
STATIC UINT64   gShmemSize          = 0;
STATIC UINT64   gShmemSet           = 0;
STATIC BOOLEAN  gMpxyLibInitialized = FALSE;
STATIC UINTN    gShmemRefCount      = 0;

STATIC
EFI_STATUS
EFIAPI
SbiMpxySetShmem (
  IN UINT64   ShmemPhysHi,
  IN UINT64   ShmemPhysLo,
  IN UINT64   ShmemSize,
  OUT UINT64  *PrevShmemPhysHi,
  OUT UINT64  *PrevShmemPhysLo,
  OUT UINT64  *PrevShmemSize,
  BOOLEAN     ReadBackOldShmem
  )
{
  SBI_RET  Ret;
  UINT32   Flags = 0b00;
  UINT64   *PrevMemDet;

  if (ReadBackOldShmem) {
    Flags = 0b01;
  }

  Ret = SbiCall (
          SBI_EXT_MPXY,
          SBI_EXT_MPXY_SET_SHMEM,
          4,
          CPU_TO_LLE (ShmemSize),
          CPU_TO_LLE (ShmemPhysLo),
          CPU_TO_LLE (ShmemPhysHi),
          Flags
          );

  if (Ret.Error == SBI_SUCCESS) {
    if ((ShmemPhysLo == INVAL_PHYS_ADDR) && (ShmemPhysHi == INVAL_PHYS_ADDR)) {
      gShmemSize   = 0;
      gShmemPhysHi = INVAL_PHYS_ADDR;
      gShmemPhysLo = INVAL_PHYS_ADDR;
      gShmemSet    = 0;
      return EFI_SUCCESS;
    }

    gShmemPhysLo = ShmemPhysLo;
    gShmemPhysHi = ShmemPhysHi;
    gShmemSize   = ShmemSize;
    gShmemSet    = 1;

    PrevMemDet = (UINT64 *)gShmemPhysLo;

    if (ReadBackOldShmem) {
      *PrevShmemSize   = LLE_TO_CPU (PrevMemDet[0]);
      *PrevShmemPhysLo = LLE_TO_CPU (PrevMemDet[1]);
      *PrevShmemPhysHi = LLE_TO_CPU (PrevMemDet[2]);
    }
  }

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
             INVAL_PHYS_ADDR,
             0,
             NULL,
             NULL,
             NULL,
             FALSE
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

STATIC
BOOLEAN
SbiMpxyShmemIsSet (
  VOID
  )
{
  return (gShmemSet);
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
  UINT64      OPhysHi, OPhysLo, OPhysSize;
  EFI_STATUS  Status;
  SBI_RET     Ret;
  UINT32      *Shmem = gNonChanTempShmem;
  UINTN       i;

  if (!gMpxyLibInitialized) {
    return (EFI_DEVICE_ERROR);
  }

  /* Set the shared memory to memory allocated for non-channel specific reads */
  Status = SbiMpxySetShmem (
             0,
             (UINT64)gNonChanTempShmem,
             EFI_PAGE_SIZE,
             &OPhysHi,
             &OPhysLo,
             &OPhysSize,
             TRUE /* Read back the old address */
             );

  if (EFI_ERROR (Status)) {
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

  /* Switch back to old shared memory */
  Status = SbiMpxySetShmem (
             OPhysHi,
             OPhysLo,
             OPhysSize,
             NULL,
             NULL,
             NULL,
             FALSE /* Read back the old address */
             );

  if (EFI_ERROR (Status)) {
    return Status;
  }

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
  UINT64      OPhysHi, OPhysLo, OPhysSize;
  EFI_STATUS  Status;
  SBI_RET     Ret;

  if (!gMpxyLibInitialized) {
    return (EFI_DEVICE_ERROR);
  }

  /* Set the shared memory to memory allocated for non-channel specific reads */
  Status = SbiMpxySetShmem (
             0,
             (UINT64)gNonChanTempShmem,
             EFI_PAGE_SIZE,
             &OPhysHi,
             &OPhysLo,
             &OPhysSize,
             TRUE /* Read back the old address */
             );

  if (EFI_ERROR (Status)) {
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
    gNonChanTempShmem,
    sizeof (UINT32) * NrAttrs
    );

  /* Switch back to old shared memory */
  Status = SbiMpxySetShmem (
             OPhysHi,
             OPhysLo,
             OPhysSize,
             NULL,
             NULL,
             NULL,
             FALSE /* Read back the old address */
             );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SbiMpxyChannelOpen (
  IN UINTN  ChannelId
  )
{
  UINT32      Attributes[MpxyChanAttrMax];
  UINT32      ChanDataLen;
  VOID        *SbiShmem;
  UINTN       NrEfiPages;
  EFI_STATUS  Status;

  if (SbiMpxyShmemInitialized () == FALSE) {
    Status = SbiProbeExtension (SBI_EXT_MPXY);
    ASSERT_EFI_ERROR (Status);

    //
    // Allocate memory to be shared with OpenSBI for initial MPXY communications
    // untils channels are initialized by their respective drivers.
    //
    gNonChanTempShmem = AllocateAlignedPages (
                          EFI_SIZE_TO_PAGES (MPXY_SHMEM_SIZE),
                          MPXY_SHMEM_SIZE // Align
                          );

    if (gNonChanTempShmem == NULL) {
      return (EFI_OUT_OF_RESOURCES);
    }

    gMpxyLibInitialized = TRUE;
  }

  Status = SbiMpxyReadChannelAttrs (
             ChannelId,
             0,
             MpxyChanAttrMax,
             &Attributes[0]
             );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  ChanDataLen = Attributes[MpxyChanAttrMsgDataMaxLen];
  NrEfiPages  = EFI_SIZE_TO_PAGES (ChanDataLen);

  /*
   * If shared memory is already set and if this channel's memory requirement
   * is more than the current then reallocate memory.
   */
  if (SbiMpxyShmemIsSet ()) {
    /* Does this channel needs bigger shared memory? */
    if (ChanDataLen > gShmemSize) {
      SbiShmem = AllocateAlignedPages (
                   NrEfiPages,
                   EFI_PAGE_SIZE // Align
                   );

      if (SbiShmem == NULL) {
        return (EFI_OUT_OF_RESOURCES);
      }

      /* Set the new shared memory */
      Status = SbiMpxySetShmem (
                 0,
                 (UINT64)SbiShmem,
                 NrEfiPages * EFI_PAGE_SIZE,
                 NULL,
                 NULL,
                 NULL,
                 FALSE /* Not interested in old memory */
                 );

      if (EFI_ERROR (Status)) {
        FreeAlignedPages (SbiShmem, NrEfiPages);
        return (EFI_DEVICE_ERROR);
      }

      /* Free the previous memory */
      if (gNrShmemPages) {
        FreeAlignedPages (gShmemVirt, gNrShmemPages);
      }

      /* Save the new shared memory */
      gShmemVirt    = SbiShmem;
      gNrShmemPages = NrEfiPages;
    }
  } else {
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
               (UINT64)SbiShmem,
               NrEfiPages * EFI_PAGE_SIZE,
               NULL,
               NULL,
               NULL,
               FALSE
               );

    if (EFI_ERROR (Status)) {
      FreeAlignedPages (SbiShmem, NrEfiPages);
      return (EFI_DEVICE_ERROR);
    }

    /* Save the new shared memory */
    gShmemVirt    = SbiShmem;
    gNrShmemPages = NrEfiPages;
  }

  /* Increase the reference count */
  gShmemRefCount++;

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SbiMpxyChannelClose (
  IN UINTN  ChannelId
  )
{
  EFI_STATUS  Status;

  if (--gShmemRefCount == 0) {
    /* Ref count is zero. Release the memory */
    Status = SbiMpxyDisableShmem ();
    if (EFI_ERROR (Status)) {
      return (EFI_DEVICE_ERROR);
    }

    FreeAlignedPages (gShmemVirt, gNrShmemPages);
  }

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

  if (!gShmemSet) {
    return EFI_DEVICE_ERROR;
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

  if ((Ret.Error == SBI_SUCCESS) && Response) {
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
