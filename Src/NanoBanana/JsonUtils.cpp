#include "APIEnvir.h"
#include "ACAPinc.h"

#include "NanoBanana/JsonUtils.hpp"

#include "JSON/Value.hpp"

#include <cstdio>

namespace NanoBanana {

std::string JsonEscape (const GS::UniString& s)
{
    const std::string in (s.ToCStr (0, MaxUSize, CC_UTF8).Get ());
    std::string out;
    out.reserve (in.size () + 16);
    for (char c : in) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char> (c) < 0x20) {
                    char buf[8];
                    snprintf (buf, sizeof (buf), "\\u%04x", c & 0xFF);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string DataUrlPayload (const GS::UniString& dataUrl)
{
    const std::string s (dataUrl.ToCStr (0, MaxUSize, CC_UTF8).Get ());
    if (s.compare (0, 5, "data:") == 0) {
        const size_t comma = s.find (',');
        if (comma != std::string::npos)
            return s.substr (comma + 1);
    }
    return s;
}

GS::UniString JsonGetString (const JSON::ObjectValue& obj, const char* key)
{
    if (!obj.HasMember (key))
        return GS::EmptyUniString;
    const JSON::ValueRef v = obj.Get (key);
    if (v && v->IsString ())
        return JSON::StringValue::Cast (*v).Get ();
    return GS::EmptyUniString;
}

} // namespace NanoBanana
