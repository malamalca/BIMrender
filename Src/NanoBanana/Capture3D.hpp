#pragma once

#ifndef NANOBANANA_CAPTURE3D_HPP
#define NANOBANANA_CAPTURE3D_HPP

#include "GSRoot.hpp"
#include "UniString.hpp"

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

// --- asynchronous capture ---------------------------------------------------
// The JS bridge can't capture directly (see above), so it schedules the
// capture as a module command executed from the main event loop:
//   1. StartAsyncCapture() posts the command (CaptureCmdID) via
//      ACAPI_AddOnAddOnCommunication_CallFromEventLoop.
//   2. CaptureCommandHandler() — installed in Initialize() — runs in command
//      context and stores the result.
//   3. The page polls FetchCaptureResult() until it stops returning "PENDING".

const GSType CaptureCmdID      = 'NBCP';
const Int32  CaptureCmdVersion = 1;

// Schedules the capture; returns false if the command could not be posted.
bool StartAsyncCapture ();

// "PENDING" while the capture is in flight; then (one-shot) the data URL,
// or "ERROR: <msg>" on failure; "" when no capture is active.
GS::UniString FetchCaptureResult ();

// Module command entry point (APIModulCommandProc).
GSErrCode CaptureCommandHandler (GSHandle params, GSPtr resultData, bool silentMode);

} // namespace NanoBanana

#endif // NANOBANANA_CAPTURE3D_HPP
