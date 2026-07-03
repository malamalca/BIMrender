#pragma once

#ifndef NANOBANANA_GEMINICLIENT_HPP
#define NANOBANANA_GEMINICLIENT_HPP

#include "UniString.hpp"

namespace NanoBanana {

// Sends an image + instruction prompt to Google's Gemini image model
// ("Nano Banana"; the exact model id is configurable in Settings) and returns
// the produced image.
//
//   apiKey          - Google Generative Language API key.
//   model           - Gemini image model id (loaded from Settings by the caller;
//                     passed in so this call never touches Archicad preferences,
//                     which are main-thread-only, from a worker thread).
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
                  const GS::UniString& model,
                  const GS::UniString& prompt,
                  const GS::UniString& inputDataUrl,
                  const GS::UniString& originalCapture,
                  const GS::Array<GS::UniString>& refImages,
                  const GS::UniString& promptHistory,
                  GS::UniString& outDataUrl,
                  GS::UniString& errMsg);

// Expands a brief instruction into a single detailed architectural-photography
// prompt using a Gemini text model. Independent of the active render provider,
// but needs a Gemini API key. Returns true and fills outText on success.
bool EnhancePromptText (const GS::UniString& apiKey,
                        const GS::UniString& userPrompt,
                        GS::UniString& outText,
                        GS::UniString& errMsg);

} // namespace NanoBanana

#endif // NANOBANANA_GEMINICLIENT_HPP
