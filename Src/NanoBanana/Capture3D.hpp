#pragma once

#ifndef NANOBANANA_CAPTURE3D_HPP
#define NANOBANANA_CAPTURE3D_HPP

#include "UniString.hpp"

namespace NanoBanana {

// True when the currently active window is the 3D model window.
bool Is3DWindowActive ();

// Captures the current 3D view to a PNG and returns it as a
// "data:image/png;base64,..." string.  Returns an empty string on failure
// (e.g. when the 3D window is not the active one).
GS::UniString CaptureCurrent3DAsDataUrl ();

} // namespace NanoBanana

#endif // NANOBANANA_CAPTURE3D_HPP
