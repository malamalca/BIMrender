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

// Schedules the task as a module command — full ACAPI command context, used
// by the 3D capture. Returns an empty string on success, otherwise an
// "ERROR: <reason>" text suitable for the panel status line (another task
// still running, or the event-loop post failed). A pending task that never
// dispatched is abandoned after 30 seconds so one hiccup can't block every
// later operation.
GS::UniString StartDeferredTask (const std::function<GS::UniString ()>& task);

// Schedules the task as a plain message-loop post (GS::MessageLoopExecutor,
// asynchronous). Used for work that opens modal dialogs (settings, native
// save dialog): dialogs need no ACAPI command context, and a DG modal dialog
// invoked from inside the module-command handler never becomes visible on
// macOS — its modal loop runs with a window that never appears, blocking
// everything. A plain run-loop iteration outside the CEF callback stack
// shows them normally. Same result/pending plumbing as StartDeferredTask.
GS::UniString StartDeferredUiTask (const std::function<GS::UniString ()>& task);

// "PENDING" while the task is in flight; then (one-shot) its result;
// "" when no task is active.
GS::UniString FetchDeferredResult ();

// Module command entry point (APIModulCommandProc).
GSErrCode DeferredCommandHandler (GSHandle params, GSPtr resultData, bool silentMode);

// TEMPORARY diagnostics for the missing settings dialog; remove once fixed.
void NbDebugLog (const char* msg, long a = 0);

} // namespace NanoBanana

#endif // NANOBANANA_CAPTURE3D_HPP
