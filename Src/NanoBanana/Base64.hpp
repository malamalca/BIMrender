#pragma once

#ifndef NANOBANANA_BASE64_HPP
#define NANOBANANA_BASE64_HPP

#include <string>

namespace NanoBanana {

// RFC 4648 base64 encode / decode over raw bytes.
std::string Base64Encode (const std::string& input);
std::string Base64Decode (const std::string& input);

} // namespace NanoBanana

#endif // NANOBANANA_BASE64_HPP
