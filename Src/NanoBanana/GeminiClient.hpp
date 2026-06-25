#pragma once

#ifndef NANOBANANA_GEMINICLIENT_HPP
#define NANOBANANA_GEMINICLIENT_HPP

#include "UniString.hpp"

namespace NanoBanana {

// Sends an image + instruction prompt to Google's gemini-2.5-flash-image
// ("Nano Banana") model and returns the produced image.
//
//   apiKey          - Google Generative Language API key.
//   prompt          - free-text editing instruction.
//   inputDataUrl    - the working image as a "data:image/...;base64,..." string.
//   originalCapture - the full-quality 3D capture as a data URL, or empty when it
//                     is the same as inputDataUrl (i.e. the first edit). When set,
//                     it is sent right after the working image as a quality source.
//   refImages       - optional user reference images (textures, furniture, etc.),
//                     each as a "data:image/...;base64,..." string. Sent after the
//                     working image (and original capture) so they can be referred
//                     to by position in the prompt.
//   promptHistory   - pipe-separated list of previous prompts, inlined as context.
//   outDataUrl      - on success, the produced image as a PNG data URL.
//   errMsg          - human-readable error on failure.
//
// Returns true on success.
bool RenderImage (const GS::UniString& apiKey,
                  const GS::UniString& prompt,
                  const GS::UniString& inputDataUrl,
                  const GS::UniString& originalCapture,
                  const GS::Array<GS::UniString>& refImages,
                  const GS::UniString& promptHistory,
                  GS::UniString& outDataUrl,
                  GS::UniString& errMsg);

} // namespace NanoBanana

#endif // NANOBANANA_GEMINICLIENT_HPP
