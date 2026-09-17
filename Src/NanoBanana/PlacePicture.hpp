#pragma once

#ifndef NANOBANANA_PLACEPICTURE_HPP
#define NANOBANANA_PLACEPICTURE_HPP

#include "GSRoot.hpp"
#include "UniString.hpp"

namespace NanoBanana {

// True when the active window can hold a figure (picture) element: the 2D
// drawing windows and the layouts. The 3D model window cannot — figures are
// 2D elements — so the panel keeps its "Place" button disabled there.
bool CanPlacePictureInCurrentWindow (GS::UniString* windowTitleOut = nullptr);

// Places a "data:image/png;base64,..." (or JPEG) image into the active 2D
// window as a figure, sized to a fraction of the current zoom and centred in
// it. Returns the window title on success, or an "ERROR: <reason>" text for
// the panel status line.
//
// Creates an element, so it must run in command context (from the module
// command of StartDeferredTask, not inline in a browser JS bridge callback).
GS::UniString PlaceDataUrlAsPicture (const GS::UniString& dataUrl);

} // namespace NanoBanana

#endif // NANOBANANA_PLACEPICTURE_HPP
