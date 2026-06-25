#pragma once

#ifndef NANOBANANA_WEBUICLIENT_HPP
#define NANOBANANA_WEBUICLIENT_HPP

#include "UniString.hpp"

namespace NanoBanana {

// Sends an image + instruction prompt to a self-hosted Stable-Diffusion WebUI
// (AUTOMATIC1111 / Forge) img2img endpoint and returns the edited image.
//
// This is a single synchronous call to {baseUrl}/sdapi/v1/img2img:
//   - the input image is passed as base64 in "init_images"
//   - the produced image comes back as base64 in "images"[0]
//
// The model used is whatever checkpoint is currently loaded in the WebUI; the
// add-on does not switch it.
//
//   baseUrl       - WebUI base, e.g. "http://127.0.0.1:7860".  May carry inline
//                   HTTP Basic credentials ("http://user:pass@host:port") for a
//                   server launched with --api-auth.
//   prompt        - free-text editing instruction.
//   inputDataUrl  - the working image as a "data:image/...;base64,..." string.
//   maskDataUrl   - optional region mask (white = edit area on black).  When
//                   non-empty the call becomes a precise INPAINT confined to the
//                   mask; when empty it is a whole-image img2img.
//   promptHistory - pipe-separated previous prompts, inlined as context.
//   outDataUrl    - on success, the produced image as a PNG data URL.
//   errMsg        - human-readable error on failure.
//
// Returns true on success.
bool WebUIRenderImage (const GS::UniString& baseUrl,
                       const GS::UniString& prompt,
                       const GS::UniString& inputDataUrl,
                       const GS::UniString& maskDataUrl,
                       const GS::UniString& promptHistory,
                       GS::UniString& outDataUrl,
                       GS::UniString& errMsg);

} // namespace NanoBanana

#endif // NANOBANANA_WEBUICLIENT_HPP
