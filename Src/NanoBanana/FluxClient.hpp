#pragma once

#ifndef NANOBANANA_FLUXCLIENT_HPP
#define NANOBANANA_FLUXCLIENT_HPP

#include "UniString.hpp"

namespace NanoBanana {

// Sends an image + instruction prompt to a Black Forest Labs FLUX.1 Kontext
// endpoint and returns the edited image.  The BFL API is asynchronous: this
// submits the task, polls until it is ready, then downloads the result.
//
//   apiKey        - BFL x-key (empty for a keyless self-hosted server).
//   baseUrl       - API base, e.g. "https://api.bfl.ai" or a self-hosted URL.
//   model         - Flux model / endpoint id, e.g. "flux-kontext-pro".
//   prompt        - free-text editing instruction.
//   inputDataUrl  - the working image as a "data:image/...;base64,..." string.
//   maskDataUrl   - optional region mask (white = edit area on black).  When
//                   non-empty the request goes to the FLUX.1 Fill endpoint and
//                   is confined to the mask; when empty it uses {model} (Kontext).
//   promptHistory - pipe-separated previous prompts, inlined as context.
//   outDataUrl    - on success, the produced image as a PNG data URL.
//   errMsg        - human-readable error on failure.
//
// Returns true on success.
bool FluxRenderImage (const GS::UniString& apiKey,
                      const GS::UniString& baseUrl,
                      const GS::UniString& model,
                      const GS::UniString& prompt,
                      const GS::UniString& inputDataUrl,
                      const GS::UniString& maskDataUrl,
                      const GS::UniString& promptHistory,
                      GS::UniString& outDataUrl,
                      GS::UniString& errMsg);

} // namespace NanoBanana

#endif // NANOBANANA_FLUXCLIENT_HPP
