/** @file
  This module implements functions to be used by SSE client

  Copyright (c) 2024, Rivos, Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef SBI_SSE_H_
#define SBI_SSE_H_

typedef
VOID
(*SSE_EVENT_CALLBACK) (
  IN UINT32  EventId,
  IN VOID   *Arg
  );

typedef struct {
  UINT32              EventId;
  VOID                *Args;
  SSE_EVENT_CALLBACK  Callback;
} SSE_EVENT_CONTEXT;

VOID
EFIAPI
_SseEntryPoint ();

EFI_STATUS
EFIAPI
SbiSseRegisterEvent (IN UINT32 EventId, IN VOID *EventArgs, IN SSE_EVENT_CALLBACK EventCallback);

EFI_STATUS
EFIAPI
SbiSseEnableEvent (IN UINT32 EventId);

VOID
EFIAPI
SbiSseEntryPoint (IN SSE_EVENT_CONTEXT *Context);

#endif

