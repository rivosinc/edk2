/** @file
  SSE support functions

Copyright (c) 2024, Rivos Inc. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiMm.h>

#include <StandaloneMmCpu.h>

#include <PiPei.h>
#include <Guid/MmramMemoryReserve.h>
#include <Guid/MpInformation.h>

#include <Library/RiscV64/StandaloneMmRiscvSse.h>

#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/BaseLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/SerialPortLib.h>
#include <Library/PcdLib.h>

#include <Library/BaseRiscVSbiLib.h>

/**
  Register an SSE (Secure Supervisor Execution) event.

  This function registers a callback for a specified SSE event ID by calling the
  SBI (Supervisor Binary Interface) extension. It allocates a context structure
  to hold the event information and callback, and registers the event with the SBI.

  @param[in] EventId         The SSE event ID to register.
  @param[in] EventArgs       Pointer to arguments that will be passed to the event callback.
  @param[in] EventCallback   The callback function to be invoked when the SSE event occurs.

  @retval EFI_SUCCESS           The SSE event was registered successfully.
  @retval EFI_UNSUPPORTED       The SSE extension is not supported.
  @retval EFI_OUT_OF_RESOURCES  Failed to allocate memory for the event context.
  @retval Other                 An error occurred during the SBI call.
**/
EFI_STATUS
EFIAPI
SbiSseRegisterEvent (IN UINT32 EventId, IN VOID *EventArgs, IN SSE_EVENT_CALLBACK EventCallback)
{
  EFI_STATUS        Status;
  SBI_RET           Ret;
  SSE_EVENT_CONTEXT *Context;

  Status = SbiProbeExtension (SBI_EXT_SSE);
  if (Status != EFI_SUCCESS) {
    return Status;
  }

  Context = AllocatePool (sizeof(*Context));
  ASSERT (Context != NULL);

  Context->Args = EventArgs;
  Context->Callback = EventCallback;
  Context->EventId = EventId;
  Ret = SbiCall (SBI_EXT_SSE, SBI_SSE_EVENT_REGISTER, 3, EventId, _SseEntryPoint, Context);

  return TranslateError (Ret.Error);
}

/**
  Enable an SSE event.

  This function enables a previously registered SSE event by calling the SBI extension.

  @param[in] EventId   The SSE event ID to enable.

  @retval EFI_SUCCESS           The SSE event was enabled successfully.
  @retval EFI_UNSUPPORTED       The SSE extension is not supported.
  @retval Other                 An error occurred during the SBI call.
**/
EFI_STATUS
EFIAPI
SbiSseEnableEvent (IN UINT32 EventId)
{
  SBI_RET Ret;

  Ret = SbiCall (SBI_EXT_SSE, SBI_SSE_EVENT_ENABLE, 1, EventId);

  return TranslateError (Ret.Error);
}

/**
  Entry point for SSE event callbacks.

  This function is called by the SBI when an SSE event occurs. It invokes the
  registered callback function with the provided context.

  @param[in] Context   Pointer to the SSE event context containing the event ID and callback arguments.

**/
VOID
EFIAPI
SbiSseEntryPoint (IN SSE_EVENT_CONTEXT *Context)
{
  Context->Callback (Context->EventId, Context->Args);
}
