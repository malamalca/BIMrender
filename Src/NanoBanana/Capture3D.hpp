#pragma once

#ifndef NANOBANANA_CAPTURE3D_HPP
#define NANOBANANA_CAPTURE3D_HPP

#include "GSRoot.hpp"
#include "UniString.hpp"

#include <functional>

namespace NanoBanana {

// True when the currently active window is the 3D model window.
bool Is3DWindowActive ();

// Captures the current 3D view to a PNG and returns it as a
// "data:image/png;base64,..." string.  Returns an empty string on failure
// (e.g. when the 3D window is not the active one); when saveErrOut is given,
// it receives the error of the underlying picture export (notably
// APIERR_REFUSEDCMD, which the Demo version returns for every save).
//
// Calls ACAPI_ProjectOperation_Save, which is refused (APIERR_REFUSEDCMD)
// outside a proper add-on command context — e.g. inside a browser JS bridge
// callback. Use the asynchronous flow below from such contexts.
GS::UniString CaptureCurrent3DAsDataUrl (GSErrCode* saveErrOut = nullptr);

// --- deferred tasks ----------------------------------------------------------
// Work that is illegal inside a browser JS bridge callback — the picture
// export above, and anything opening a modal dialog (settings, the native
// save dialog), which nests an event loop inside a blocked CEF IPC call and
// crashes — is executed as a module command from the main event loop instead:
//   1. StartDeferredTask() stores the task and posts the command
//      (DeferredCmdID) via ACAPI_AddOnAddOnCommunication_CallFromEventLoop.
//   2. DeferredCommandHandler() — installed in Initialize() — runs it in
//      command context and stores its result string.
//   3. The page polls FetchDeferredResult() until it stops returning
//      "PENDING".
// One task can be in flight at a time; StartDeferredTask refuses a second.

const GSType DeferredCmdID      = 'NBDF';
const Int32  DeferredCmdVersion = 1;

// Schedules the task; returns false if one is already running or the
// command could not be posted.
bool StartDeferredTask (const std::function<GS::UniString ()>& task);

// "PENDING" while the task is in flight; then (one-shot) its result;
// "" when no task is active.
GS::UniString FetchDeferredResult ();

// Module command entry point (APIModulCommandProc).
GSErrCode DeferredCommandHandler (GSHandle params, GSPtr resultData, bool silentMode);

} // namespace NanoBanana

#endif // NANOBANANA_CAPTURE3D_HPP
