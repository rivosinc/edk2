/** @file
  Entry point to the Standalone MM Foundation on RiscV platform.

Copyright (c) 2017 - 2021, Arm Ltd. All rights reserved.<BR>
Copyright (c) 2023, Ventana Micro System Inc. All rights reserved.<BR>
Copyright (c) 2023, Intel Corporation. All rights reserved.<BR>

SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef __STANDALONEMMCORE_ENTRY_POINT_H__
#define __STANDALONEMMCORE_ENTRY_POINT_H__

#include <Library/PeCoffLib.h>
#include <Library/FvLib.h>

#define CPU_INFO_FLAG_PRIMARY_CPU  0x00000001

typedef struct {
  UINT32    ProcessorId;
  UINT32    Package;
  UINT32    Core;
  UINT32    Flags;
} EFI_RISCV_SMM_CPU_INFO;

typedef struct {
  UINT64                    MmMemBase;
  UINT64                    MmMemLimit;
  UINT64                    MmImageBase;
  UINT64                    MmStackBase;
  UINT64                    MmHeapBase;
  UINT64                    MmNsCommBufBase;
  UINT64                    MmSharedBufBase;
  UINT64                    MmImageSize;
  UINT64                    MmPcpuStackSize;
  UINT64                    MmHeapSize;
  UINT64                    MmNsCommBufSize;
  UINT64                    MmSharedBufSize;
  UINT32                    NumMmMemRegions;
  UINT32                    NumCpus;
  UINT32                    MpxyChannelId;
  EFI_RISCV_SMM_CPU_INFO    CpuInfo;
} EFI_RISCV_SMM_PAYLOAD_INFO;

#define BOOT_INFO_STACK_BASE_OFFSET  24   // Used in assembly
STATIC_ASSERT (BOOT_INFO_STACK_BASE_OFFSET == OFFSET_OF (EFI_RISCV_SMM_PAYLOAD_INFO, MmStackBase));
typedef RETURN_STATUS (*REGION_PERMISSION_UPDATE_FUNC) (
  IN  EFI_PHYSICAL_ADDRESS  BaseAddress,
  IN  UINT64                Length
  );

// RPMI Messages
#define RPMI_REQFWD_ENABLE_NOTIFICATION       0x1
#define RPMI_REQFWD_RETRIEVE_CURRENT_MESSAGE  0x2
#define RPMI_REQFWD_COMPLETE_CURRENT_MESSAGE  0x3

// SMM Message ID
#define RISCV_MSG_ID_SMM_VERSION         0x1
#define RISCV_MSG_ID_SMM_COMMUNICATE     0x2
#define RISCV_MSG_ID_SMM_EVENT_COMPLETE  0x3

// SMM return error codes
#define RISCV_SMM_RET_SUCCESS         0
#define RISCV_SMM_RET_NOT_SUPPORTED   -1
#define RISCV_SMM_RET_INVALID_PARAMS  -2
#define RISCV_SMM_RET_DENIED          -3
#define RISCV_SMM_RET_NO_MEMORY       -4

#pragma pack(1) // Ensure no padding between fields
// RPMI message header structure
typedef struct {
  UINT16    ServiceGroupId;      // SERVICEGROUP_ID (16 bits, bits 15:0)
  UINT8     ServiceId;           // SERVICE_ID (8 bits, bits 23:16)
  UINT8     Flags;               // FLAGS (8 bits, bits 31:24)
  UINT16    Token;               // TOKEN (16 bits, bits 31:16 of the second word)
  UINT16    DataLen;             // DATALEN (16 bits, bits 15:0 of the second word, length of data in bytes, 4-byte multiple)
} RPMI_MESSAGE_HEADER;

typedef struct {
  RPMI_MESSAGE_HEADER    hdr;
  UINT32                 StartIndex;
} REQFWD_RETRIEVE_CMD;

typedef struct {
  UINT32    Offset;
} MM_PAYLOAD;

typedef struct {
  RPMI_MESSAGE_HEADER    hdr;
  UINT32                 Status;
  UINT32                 Remaining;
  UINT32                 Returned;
  UINT32                 Offset;
} REQFWD_RETRIEVE_RESP;

typedef struct {
  RPMI_MESSAGE_HEADER    Header; // Message header
  UINT8                  Data[]; // Data (variable length, 4-byte multiple based on DataLen)
} RPMI_MESSAGE;
#pragma pack() // End of packed structure

typedef struct {
  RPMI_MESSAGE_HEADER    hdr;
  UINTN                  Arg0;
  UINTN                  Arg1;
} RISCV_SMM_MSG_COMM_ARGS;

#define RISCV_SMM_MSG_SHMEM_SIZE  0x2000

/**
  Privileged firmware assigns RO & Executable attributes to all memory occupied
  by the Boot Firmware Volume. This function sets the correct permissions of
  sections in the Standalone MM Core module to be able to access RO and RW data
  and make further progress in the boot process.

  @param  [in] ImageContext           Pointer to PE/COFF image context
  @param  [in] ImageBase              Base of image in memory
  @param  [in] SectionHeaderOffset    Offset of PE/COFF image section header
  @param  [in] NumberOfSections       Number of Sections
  @param  [in] TextUpdater            Function to change code permissions
  @param  [in] ReadOnlyUpdater        Function to change RO permissions
  @param  [in] ReadWriteUpdater       Function to change RW permissions

**/
EFI_STATUS
EFIAPI
UpdateMmFoundationPeCoffPermissions (
  IN  CONST PE_COFF_LOADER_IMAGE_CONTEXT  *ImageContext,
  IN  EFI_PHYSICAL_ADDRESS                ImageBase,
  IN  UINT32                              SectionHeaderOffset,
  IN  CONST  UINT16                       NumberOfSections,
  IN  REGION_PERMISSION_UPDATE_FUNC       TextUpdater,
  IN  REGION_PERMISSION_UPDATE_FUNC       ReadOnlyUpdater,
  IN  REGION_PERMISSION_UPDATE_FUNC       ReadWriteUpdater
  );

/**
  Privileged firmware assigns RO & Executable attributes to all memory occupied
  by the Boot Firmware Volume. This function locates the section information of
  the Standalone MM Core module to be able to change permissions of the
  individual sections later in the boot process.

  @param  [in]      TeData                Pointer to PE/COFF image data
  @param  [in, out] ImageContext          Pointer to PE/COFF image context
  @param  [out]     ImageBase             Pointer to ImageBase variable
  @param  [in, out] SectionHeaderOffset   Offset of PE/COFF image section header
  @param  [in, out] NumberOfSections      Number of Sections

**/
EFI_STATUS
EFIAPI
GetStandaloneMmCorePeCoffSections (
  IN        VOID                          *TeData,
  IN  OUT   PE_COFF_LOADER_IMAGE_CONTEXT  *ImageContext,
  OUT   EFI_PHYSICAL_ADDRESS              *ImageBase,
  IN  OUT   UINT32                        *SectionHeaderOffset,
  IN  OUT   UINT16                        *NumberOfSections
  );

/**
  Privileged firmware assigns RO & Executable attributes to all memory occupied
  by the Boot Firmware Volume. This function locates the Standalone MM Core
  module PE/COFF image in the BFV and returns this information.

  @param  [in]      BfvAddress         Base Address of Boot Firmware Volume
  @param  [in, out] TeData             Pointer to address for allocating memory
                                       for PE/COFF image data
  @param  [in, out] TeDataSize         Pointer to size of PE/COFF image data

**/
EFI_STATUS
EFIAPI
LocateStandaloneMmCorePeCoffData (
  IN        EFI_FIRMWARE_VOLUME_HEADER  *BfvAddress,
  IN  OUT   VOID                        **TeData,
  IN  OUT   UINTN                       *TeDataSize
  );

/**
  Use the boot information passed by privileged firmware to populate a HOB list
  suitable for consumption by the MM Core and drivers.

  @param  [in, out] CpuDriverEntryPoint   Address of MM CPU driver entrypoint
  @param  [in]      PayloadBootInfo       Boot information passed by privileged
                                          firmware

**/
VOID *
EFIAPI
CreateHobListFromBootInfo (
  IN  OUT  PI_MM_CPU_DRIVER_ENTRYPOINT  *CpuDriverEntryPoint,
  IN       EFI_RISCV_SMM_PAYLOAD_INFO   *PayloadBootInfo
  );

/**
  The entry point of Standalone MM Foundation.

  @param  [in]  CpuId             The Id assigned to this running CPU
  @param  [in]  PayloadInfoAddress   The address of payload info

**/
VOID
EFIAPI
CModuleEntryPoint (
  IN UINT64  CpuId,
  IN VOID    *PayloadInfoAddress
  );

/**
  Auto generated function that calls the library constructors for all of the module's dependent libraries.

  This function must be called by _ModuleEntryPoint().
  This function calls the set of library constructors for the set of library instances
  that a module depends on.  This includes library instances that a module depends on
  directly and library instances that a module depends on indirectly through other
  libraries. This function is auto generated by build tools and those build tools are
  responsible for collecting the set of library instances, determine which ones have
  constructors, and calling the library constructors in the proper order based upon
  each of the library instances own dependencies.

  @param  ImageHandle  The image handle of the DXE Core.
  @param  SystemTable  A pointer to the EFI System Table.

**/
// VOID
// EFIAPI
// ProcessLibraryConstructorList (
//   IN EFI_HANDLE           ImageHandle,
//   IN EFI_MM_SYSTEM_TABLE  *MmSystemTable
//   );

/**
  Auto generated function that calls a set of module entry points.

  This function must be called by _ModuleEntryPoint().
  This function calls the set of module entry points.
  This function is auto generated by build tools and those build tools are responsible
  for collecting the module entry points and calling them in a specified order.

  @param  HobStart  Pointer to the beginning of the HOB List passed in from the PEI Phase.

**/
VOID
EFIAPI
ProcessModuleEntryPointList (
  IN VOID  *HobStart
  );

#endif
