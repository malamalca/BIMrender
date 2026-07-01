#pragma once

#ifndef NANOBANANA_JSONUTILS_HPP
#define NANOBANANA_JSONUTILS_HPP

#include <string>

#include "UniString.hpp"
#include "JSON/Value.hpp"

namespace NanoBanana {

// Minimal JSON string escaping for prompt text embedded in a request body.
std::string JsonEscape (const GS::UniString& s);

// Split a "data:<mime>;base64,<payload>" URL into its raw base64 payload.
// Falls back to treating the whole string as raw base64.
std::string DataUrlPayload (const GS::UniString& dataUrl);

// Read a string member from a JSON object (empty when absent / not a string).
GS::UniString JsonGetString (const JSON::ObjectValue& obj, const char* key);

} // namespace NanoBanana

#endif // NANOBANANA_JSONUTILS_HPP
