// *****************************************************************************
// Moving a rendered image between the panel and disk: writing one out (a
// "data:image/…;base64,…" URL) through the host's native Save dialog or
// straight into a default folder, and reading one in through the native Open
// dialog.
// *****************************************************************************

#pragma once

#ifndef NANOBANANA_SAVEIMAGEFILE_HPP
#define NANOBANANA_SAVEIMAGEFILE_HPP

#include "UniString.hpp"

namespace NanoBanana {

// Result sentinel returned when the user dismissed the dialog. It is not an
// error: nothing happened and nothing needs reporting beyond "cancelled".
extern const char* const SaveCancelled;      // "CANCELLED"

// Asks the user for a destination through the host's native Save dialog and
// writes the image there. Returns the saved file's path, SaveCancelled, or
// "ERROR: <reason>".
//
// Must NOT be called from a browser JS-bridge callback: the dialog nests an
// event loop inside the blocked CEF IPC call, which crashes Archicad. Run it
// from a deferred UI task (NanoBanana::StartDeferredUiTask), the same context
// the settings dialog uses.
GS::UniString SaveImageWithDialog (const GS::UniString& dataUrl);

// Writes the image into <Documents>/BIMrender under a non-colliding name,
// without asking, and reveals it in the OS file browser. Returns the saved
// file's path or "ERROR: <reason>". Safe to call inline: it opens no dialog and
// pumps no event loop.
GS::UniString SaveImageToDefaultFolder (const GS::UniString& dataUrl);

// Asks the user for an image file through the host's native Open dialog and
// returns its contents as a "data:<mime>;base64,…" URL, ready to hand to the
// page as the working image. Returns SaveCancelled when the dialog was
// dismissed, or "ERROR: <reason>".
//
// The format is taken from the file's own magic bytes, not its name: only PNG,
// JPEG and WebP are accepted, since those are what the page and the image
// backends handle.
//
// Carries the same restriction as SaveImageWithDialog: not from a browser
// JS-bridge callback, only from a deferred UI task.
GS::UniString LoadImageWithDialog ();

// True when the native file dialogs - Save and Open - can be used in this
// build. False on platforms where they cannot be driven from the browser bridge
// yet (see the comment on the implementation); the caller then falls back to
// SaveImageToDefaultFolder for saving, and to the page's own file input for
// opening.
bool IsFileDialogAvailable ();

} // namespace NanoBanana

#endif // NANOBANANA_SAVEIMAGEFILE_HPP
