#include "NanoBanana/Base64.hpp"

namespace NanoBanana {

static const char* kEncTable =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64Encode (const std::string& input)
{
    std::string out;
    out.reserve (((input.size () + 2) / 3) * 4);

    const unsigned char* data = reinterpret_cast<const unsigned char*> (input.data ());
    const size_t n = input.size ();
    size_t i = 0;
    while (i + 3 <= n) {
        const unsigned int v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += kEncTable[(v >> 18) & 0x3F];
        out += kEncTable[(v >> 12) & 0x3F];
        out += kEncTable[(v >> 6) & 0x3F];
        out += kEncTable[v & 0x3F];
        i += 3;
    }

    const size_t rem = n - i;
    if (rem == 1) {
        const unsigned int v = data[i] << 16;
        out += kEncTable[(v >> 18) & 0x3F];
        out += kEncTable[(v >> 12) & 0x3F];
        out += '=';
        out += '=';
    } else if (rem == 2) {
        const unsigned int v = (data[i] << 16) | (data[i + 1] << 8);
        out += kEncTable[(v >> 18) & 0x3F];
        out += kEncTable[(v >> 12) & 0x3F];
        out += kEncTable[(v >> 6) & 0x3F];
        out += '=';
    }

    return out;
}

std::string Base64Decode (const std::string& input)
{
    auto decodeChar = [] (char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1; // padding or whitespace
    };

    std::string out;
    out.reserve ((input.size () / 4) * 3);

    int buffer = 0;
    int bits   = 0;
    for (char c : input) {
        const int val = decodeChar (c);
        if (val < 0)
            continue; // skip '=', newlines, etc.
        buffer = (buffer << 6) | val;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char> ((buffer >> bits) & 0xFF);
        }
    }

    return out;
}

} // namespace NanoBanana
