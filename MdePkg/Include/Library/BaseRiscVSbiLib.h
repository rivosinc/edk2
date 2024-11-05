/** @file
  Library to call the RISC-V SBI ecalls

  Copyright (c) 2021-2022, Hewlett Packard Development LP. All rights reserved.<BR>
  Copyright (c) 2023, Intel Corporation. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

  @par Glossary:
    - Hart - Hardware Thread, similar to a CPU core

  Currently, EDK2 needs to call SBI only to set the time and to do system reset.

**/

#ifndef RISCV_SBI_LIB_H_
#define RISCV_SBI_LIB_H_

#include <Uefi.h>

/* SBI Extension IDs */
#define SBI_EXT_0_1_CONSOLE_PUTCHAR  0x1
#define SBI_EXT_0_1_CONSOLE_GETCHAR  0x2
#define SBI_EXT_BASE                 0x10
#define SBI_EXT_DBCN                 0x4442434E
#define SBI_EXT_TIME                 0x54494D45
#define SBI_EXT_SRST                 0x53525354
#define SBI_EXT_MPXY                 0x4D505859
#define SBI_EXT_SSE                  0x535345

/* SBI function IDs for base extension */
#define SBI_EXT_BASE_SPEC_VERSION   0x0
#define SBI_EXT_BASE_IMPL_ID        0x1
#define SBI_EXT_BASE_IMPL_VERSION   0x2
#define SBI_EXT_BASE_PROBE_EXT      0x3
#define SBI_EXT_BASE_GET_MVENDORID  0x4
#define SBI_EXT_BASE_GET_MARCHID    0x5
#define SBI_EXT_BASE_GET_MIMPID     0x6

/* SBI function IDs for DBCN extension */
#define SBI_EXT_DBCN_WRITE       0x0
#define SBI_EXT_DBCN_READ        0x1
#define SBI_EXT_DBCN_WRITE_BYTE  0x2

/* SBI function IDs for TIME extension */
#define SBI_EXT_TIME_SET_TIMER  0x0

/* SBI function IDs for SRST extension */
#define SBI_EXT_SRST_RESET  0x0

/* SBI function IDs. for MPXY extension */
#define SBI_EXT_MPXY_SET_SHMEM                0x0
#define SBI_EXT_MPXY_GET_CHANNEL_IDS          0x1
#define SBI_EXT_MPXY_READ_ATTRS               0x2
#define SBI_EXT_MPXY_WRITE_ATTRS              0x3
#define SBI_EXT_MPXY_SEND_MSG_WITH_RESP       0x4
#define SBI_EXT_MPXY_SEND_MSG_NO_RESP         0x5
#define SBI_EXT_MPXY_GET_NOTIFICATION_EVENTS  0x6

#define SBI_SRST_RESET_TYPE_SHUTDOWN     0x0
#define SBI_SRST_RESET_TYPE_COLD_REBOOT  0x1
#define SBI_SRST_RESET_TYPE_WARM_REBOOT  0x2

#define SBI_SRST_RESET_REASON_NONE     0x0
#define SBI_SRST_RESET_REASON_SYSFAIL  0x1


#define SBI_SSE_EVENT_ATTR_READ       0
#define SBI_SSE_EVENT_ATTR_WRITE      1
#define SBI_SSE_EVENT_REGISTER        2
#define SBI_SSE_EVENT_UNREGISTER      3
#define SBI_SSE_EVENT_ENABLE          4
#define SBI_SSE_EVENT_DISABLE         5
#define SBI_SSE_EVENT_COMPLETE        6
#define SBI_SSE_EVENT_SIGNAL          7

/* SBI SSE Event Attributes. */
typedef enum {
	SBI_SSE_ATTR_STATUS		= 0x00000000,
	SBI_SSE_ATTR_PRIO		= 0x00000001,
	SBI_SSE_ATTR_CONFIG		= 0x00000002,
	SBI_SSE_ATTR_PREFERRED_HART	= 0x00000003,
	SBI_SSE_ATTR_ENTRY_PC		= 0x00000004,
	SBI_SSE_ATTR_ENTRY_ARG		= 0x00000005,
	SBI_SSE_ATTR_INTERRUPTED_SEPC	= 0x00000006,
	SBI_SSE_ATTR_INTERRUPTED_FLAGS	= 0x00000007,
	SBI_SSE_ATTR_INTERRUPTED_A6	= 0x00000008,
	SBI_SSE_ATTR_INTERRUPTED_A7	= 0x00000009,

	SBI_SSE_ATTR_MAX		= 0x0000000A
}  SBI_SSE_ATTR_ID;

#define SBI_SSE_ATTR_STATUS_STATE_OFFSET	0
#define SBI_SSE_ATTR_STATUS_STATE_MASK		0x3
#define SBI_SSE_ATTR_STATUS_PENDING_OFFSET	2
#define SBI_SSE_ATTR_STATUS_INJECT_OFFSET	3

#define SBI_SSE_ATTR_CONFIG_ONESHOT	(1 << 0)

#define SBI_SSE_ATTR_INTERRUPTED_FLAGS_STATUS_SPP	(1 << 0)
#define SBI_SSE_ATTR_INTERRUPTED_FLAGS_STATUS_SPIE	(1 << 1)
#define SBI_SSE_ATTR_INTERRUPTED_FLAGS_HSTATUS_SPV	(1 << 2)
#define SBI_SSE_ATTR_INTERRUPTED_FLAGS_HSTATUS_SPVP	(1 << 3)

#define SBI_SSE_EVENT_LOCAL_RAS		0x00000000
#define SBI_SSE_EVENT_GLOBAL_RAS	0x00008000
#define SBI_SSE_EVENT_LOCAL_PMU		0x00010000
#define SBI_SSE_EVENT_LOCAL_SOFTWARE	0xffff0000
#define SBI_SSE_EVENT_GLOBAL_SOFTWARE	0xffff8000

#define SBI_SSE_EVENT_GLOBAL		(1 << 15)
#define SBI_SSE_EVENT_PLATFORM		(1 << 14)

/* SBI return error codes */
#define SBI_SUCCESS                0
#define SBI_ERR_FAILED             -1
#define SBI_ERR_NOT_SUPPORTED      -2
#define SBI_ERR_INVALID_PARAM      -3
#define SBI_ERR_DENIED             -4
#define SBI_ERR_INVALID_ADDRESS    -5
#define SBI_ERR_ALREADY_AVAILABLE  -6
#define SBI_ERR_ALREADY_STARTED    -7
#define SBI_ERR_ALREADY_STOPPED    -8
#define SBI_ERR_NO_SHMEM           -9
#define SBI_ERR_INVALID_STATE      -10
#define SBI_ERR_BAD_RANGE          -11
#define SBI_ERR_NOT_IMPLEMENTED    -12
#define SBI_ERR_TIMEOUT            -13
#define SBI_ERR_IO                 -14

#define SBI_LAST_ERR  SBI_ERR_IO

typedef struct {
  UINT64    BootHartId;
  VOID      *PeiServiceTable;    // PEI Service table
  VOID      *PrePiHobList;       // Pre PI Hob List
  UINT64    FlattenedDeviceTree; // Pointer to Flattened Device tree
} EFI_RISCV_FIRMWARE_CONTEXT;

//
// EDK2 OpenSBI firmware extension return status.
//
typedef struct {
  UINTN    Error; ///< SBI status code
  UINTN    Value; ///< Value returned
} SBI_RET;

SBI_RET
EFIAPI
SbiCall (
  IN  UINTN  ExtId,
  IN  UINTN  FuncId,
  IN  UINTN  NumArgs,
  ...
  );

EFI_STATUS
EFIAPI
TranslateError (
  IN  UINTN  SbiError
  );

VOID
EFIAPI
SbiSetTimer (
  IN  UINT64  Time
  );

EFI_STATUS
EFIAPI
SbiSystemReset (
  IN  UINTN  ResetType,
  IN  UINTN  ResetReason
  );

/**
  Get firmware context of the calling hart.

  @param[out] FirmwareContext      The firmware context pointer.
**/
VOID
EFIAPI
GetFirmwareContext (
  OUT EFI_RISCV_FIRMWARE_CONTEXT  **FirmwareContext
  );

/**
  Set firmware context of the calling hart.

  @param[in] FirmwareContext       The firmware context pointer.
**/
VOID
EFIAPI
SetFirmwareContext (
  IN EFI_RISCV_FIRMWARE_CONTEXT  *FirmwareContext
  );

/**
  Get pointer to OpenSBI Firmware Context

  Get the pointer of firmware context.

  @param    FirmwareContextPtr   Pointer to retrieve pointer to the
                                 Firmware Context.
**/
VOID
EFIAPI
GetFirmwareContextPointer (
  IN OUT EFI_RISCV_FIRMWARE_CONTEXT  **FirmwareContextPtr
  );

/**
  Set pointer to OpenSBI Firmware Context

  Set the pointer of firmware context.

  @param    FirmwareContextPtr   Pointer to Firmware Context.
**/
VOID
EFIAPI
SetFirmwareContextPointer (
  IN EFI_RISCV_FIRMWARE_CONTEXT  *FirmwareContextPtr
  );

/**
  Probe support for an extension in OpenSBI

  Check if the extension is supported by SBI

  @param    Extension   Extension ID to be probed
**/
EFI_STATUS
EFIAPI
SbiProbeExtension (
  IN UINTN  Extension
  );

/**
  Make ECALL in assembly

  Switch to M-mode

  @param[in,out]   Arg0
  @param[in,out]   Arg1
  @param[in]       Arg2
  @param[in]       Arg3
  @param[in]       Arg4
  @param[in]       Arg5
  @param[in]       FID
  @param[in]       EXT
**/
VOID
EFIAPI
RiscVSbiEcall (
  IN OUT UINTN  *Arg0,
  IN OUT UINTN  *Arg1,
  IN UINTN      Arg2,
  IN UINTN      Arg3,
  IN UINTN      Arg4,
  IN UINTN      Arg5,
  IN UINTN      Fid,
  IN UINTN      Ext
  );

#endif
