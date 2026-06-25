#include <string>

#include "APIEnvir.h"
#include "ACAPinc.h"

#include "NanoBanana/WebUIClient.hpp"
#include "NanoBanana/Base64.hpp"

#include "HTTP/Client/ClientConnection.hpp"
#include "IBinaryChannelUtilities.hpp"
#include "IOBinProtocolXs.hpp"
#include "IChannelX.hpp"

#include "JSON/JDOMParser.hpp"
#include "JSON/Value.hpp"

namespace NanoBanana {

// img2img defaults.  Denoising strength balances "apply the edit" against
// "preserve the input"; ~0.6 keeps the architecture recognisable while still
// letting the prompt change materials / lighting.
static const double kDenoisingStrength = 0.6;
static const int    kSteps             = 30;

// ---------------------------------------------------------------------------
// Minimal JSON string escaping for the prompt text.
// ---------------------------------------------------------------------------
static std::string JsonEscape (const GS::UniString& s)
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

// ---------------------------------------------------------------------------
// Split a "data:<mime>;base64,<payload>" URL into its raw base64 payload.
// Falls back to treating the whole string as raw base64.
// ---------------------------------------------------------------------------
static std::string DataUrlPayload (const GS::UniString& dataUrl)
{
    const std::string s (dataUrl.ToCStr (0, MaxUSize, CC_UTF8).Get ());
    if (s.compare (0, 5, "data:") == 0) {
        const size_t comma = s.find (',');
        if (comma != std::string::npos)
            return s.substr (comma + 1);
    }
    return s;
}

// ---------------------------------------------------------------------------
// Read the pixel dimensions from a decoded PNG (IHDR width/height, big-endian).
// Returns false if the bytes are not a PNG we can read.
// ---------------------------------------------------------------------------
static bool PngDimensions (const std::string& png, int& width, int& height)
{
    // 8-byte signature, 4-byte length, "IHDR", then width(4) + height(4).
    if (png.size () < 24)
        return false;
    const unsigned char* p = reinterpret_cast<const unsigned char*> (png.data ());
    if (p[0] != 0x89 || p[1] != 'P' || p[2] != 'N' || p[3] != 'G')
        return false;
    width  = (p[16] << 24) | (p[17] << 16) | (p[18] << 8) | p[19];
    height = (p[20] << 24) | (p[21] << 16) | (p[22] << 8) | p[23];
    return width > 0 && height > 0;
}

// ---------------------------------------------------------------------------
// Read a string member from a JSON object (empty when absent / not a string).
// ---------------------------------------------------------------------------
static GS::UniString JGetStr (const JSON::ObjectValue& obj, const char* key)
{
    if (!obj.HasMember (key))
        return GS::EmptyUniString;
    const JSON::ValueRef v = obj.Get (key);
    if (v && v->IsString ())
        return JSON::StringValue::Cast (*v).Get ();
    return GS::EmptyUniString;
}

// ---------------------------------------------------------------------------
// One HTTP exchange.  Reads the whole response body as raw bytes.
// Optional HTTP Basic auth is taken from inline userinfo in the URL.
// ---------------------------------------------------------------------------
static bool HttpPostJson (const GS::UniString& absUrl,
                          const std::string& body,
                          int& statusOut,
                          std::string& respOut,
                          GS::UniString& errMsg)
{
    using namespace HTTP::Client;
    using namespace HTTP::MessageHeader;

    try {
        IO::URI::URI uri (absUrl);

        // Reconstruct the host without any inline userinfo for the connection;
        // userinfo (user:pass) becomes an HTTP Basic Authorization header.
        GS::UniString host = uri.GetHost ();
        GS::UniString base = uri.GetScheme () + "://" + host;
        if (uri.GetPort () != 0)
            base += GS::UniString::Printf (":%u", (unsigned int) uri.GetPort ());

        GS::UniString path = uri.GetPathAndQuery ();
        if (path.IsEmpty () || path == "/")
            path = "/sdapi/v1/img2img";

        GS::UniString userInfo = uri.GetUserInfo ();   // "user:pass" or empty

        IO::URI::URI      connUrl (base);
        ClientConnection  conn (connUrl);
        conn.SetTimeout (300000);   // local generation can be slow on CPU
        conn.Connect ();

        Request request (Method::Id::Post, path);
        RequestHeaderFieldCollection& hf = request.GetRequestHeaderFieldCollection ();
        hf.Add (GS::UniString ("Content-Type"), GS::UniString ("application/json"));
        hf.Add (GS::UniString ("accept"),       GS::UniString ("application/json"));
        hf.Add (GS::UniString ("User-Agent"),   GS::UniString ("arUtils-NanoBanana"));
        if (!userInfo.IsEmpty ()) {
            const std::string ui (userInfo.ToCStr (0, MaxUSize, CC_UTF8).Get ());
            hf.Add (GS::UniString ("Authorization"),
                    GS::UniString ("Basic ") + GS::UniString (Base64Encode (ui).c_str (), CC_UTF8));
        }

        conn.Send (request, body.data (), static_cast<GS::USize> (body.size ()));
        conn.FinishSend ();

        Response response;
        GS::IBinaryChannel& ch = conn.BeginReceive (response);
        std::string data;
        char buf[16384];
        for (;;) {
            GS::USize n = 0;
            try { n = ch.Read (buf, sizeof (buf), GS::ReadSomeMode); }
            catch (...) { break; }
            if (n == 0)
                break;
            data.append (buf, n);
            if (data.size () > 64u * 1024u * 1024u)
                break;
        }
        statusOut = static_cast<int> (response.GetStatusCode ());
        conn.FinishReceive ();
        conn.Close (false);
        respOut.swap (data);
        return true;
    }
    catch (GS::Exception& ex) { errMsg = GS::UniString ("Network error: ") + ex.GetMessage (); }
    catch (...)               { errMsg = "Could not reach the local WebUI server."; }
    return false;
}

// ---------------------------------------------------------------------------
// WebUIRenderImage – single img2img call.
// ---------------------------------------------------------------------------
bool WebUIRenderImage (const GS::UniString& baseUrl,
                       const GS::UniString& prompt,
                       const GS::UniString& inputDataUrl,
                       const GS::UniString& maskDataUrl,
                       const GS::UniString& promptHistory,
                       GS::UniString& outDataUrl,
                       GS::UniString& errMsg)
{
    const std::string b64 = DataUrlPayload (inputDataUrl);
    if (b64.empty ()) {
        errMsg = "No captured image to send.";
        return false;
    }

    const std::string maskB64 = maskDataUrl.IsEmpty () ? std::string () : DataUrlPayload (maskDataUrl);
    const bool inpaint = !maskB64.empty ();

    GS::UniString base = baseUrl;
    base.Trim ();
    if (base.IsEmpty ())
        base = "http://127.0.0.1:7860";
    while (base.GetLength () > 0 && base.GetLast () == '/')
        base.DeleteLast ();

    // Compose the instruction: prior prompts as context, then the new request.
    GS::UniString turnText;
    if (!promptHistory.IsEmpty ()) {
        turnText  = "[Context - previously applied instructions, oldest first]\n";
        turnText += promptHistory;
        turnText += "\n[Current instruction]\n";
    }
    turnText += prompt;
    // When inpainting we only touch the masked area, so "keep the geometry"
    // would fight edits like "put a sofa here"; keep just the realism cues.
    turnText += inpaint ? ", photorealistic, sharp, high detail"
                        : ", photorealistic, keep the original architectural geometry, sharp, high detail";

    // For inpainting the masked area should change more than a whole-image pass.
    const double denoising = inpaint ? 0.75 : kDenoisingStrength;

    // Preserve the input resolution when we can read it from the PNG header;
    // otherwise WebUI would fall back to its 512x512 default and downscale.
    int w = 0, h = 0;
    const bool haveDims = PngDimensions (Base64Decode (b64), w, h);

    std::string body;
    body.reserve (b64.size () + maskB64.size () + 1024);
    body += "{\"init_images\":[\"";
    body += b64;
    body += "\"],\"prompt\":\"";
    body += JsonEscape (turnText);
    body += "\",\"denoising_strength\":";
    {
        char num[32];
        snprintf (num, sizeof (num), "%.3f", denoising);
        body += num;
    }
    body += ",\"steps\":";
    body += std::to_string (kSteps);
    if (haveDims) {
        body += ",\"width\":";  body += std::to_string (w);
        body += ",\"height\":"; body += std::to_string (h);
    }
    if (inpaint) {
        // White mask = the area to repaint.  inpaint_full_res renders the masked
        // region at full resolution for sharper detail; mask_blur softens the seam.
        body += ",\"mask\":\"";
        body += maskB64;
        body += "\",\"inpainting_mask_invert\":0,\"inpainting_fill\":1,"
                "\"inpaint_full_res\":true,\"inpaint_full_res_padding\":32,\"mask_blur\":4";
    }
    body += "}";

    const GS::UniString url = base + "/sdapi/v1/img2img";

    int         status = 0;
    std::string resp;
    if (!HttpPostJson (url, body, status, resp, errMsg))
        return false;

    if (status != 200) {
        if (status == 404)
            errMsg = "The server has no /sdapi/v1/img2img endpoint. Launch the WebUI with the API enabled (--api).";
        else if (status == 401)
            errMsg = "The local server rejected the credentials. Use http://user:pass@host:port if it runs with --api-auth.";
        else
            errMsg = GS::UniString::Printf ("Local server returned HTTP %d: ", status)
                   + GS::UniString (resp.c_str (), CC_UTF8);
        return false;
    }

    // Response: { "images": ["<base64>", ...], ... }
    GS::UniString imgB64;
    try {
        JSON::JDOMStringParser parser;
        JSON::ValueRef root = parser.Parse (GS::UniString (resp.c_str (), CC_UTF8));
        if (root && root->IsObject ()) {
            const JSON::ObjectValue& o = JSON::ObjectValue::Cast (*root);
            if (o.HasMember ("images")) {
                const JSON::ValueRef arr = o.Get ("images");
                if (arr && arr->IsArray ()) {
                    const JSON::ArrayValue& imgs = JSON::ArrayValue::Cast (*arr);
                    if (imgs.GetSize () > 0) {
                        const JSON::ValueRef first = imgs.Get (0);
                        if (first && first->IsString ())
                            imgB64 = JSON::StringValue::Cast (*first).Get ();
                    }
                }
            }
            // Surface an error/detail message if there was no image.
            if (imgB64.IsEmpty () && o.HasMember ("detail"))
                errMsg = GS::UniString ("Local server: ") + JGetStr (o, "detail");
        }
    } catch (...) {
        errMsg = "Could not parse the local server response.";
        return false;
    }

    if (imgB64.IsEmpty ()) {
        if (errMsg.IsEmpty ())
            errMsg = "The local server returned no image.";
        return false;
    }

    // WebUI returns a bare base64 PNG (sometimes with a data-URL prefix).
    if (imgB64.BeginsWith ("data:"))
        outDataUrl = imgB64;
    else {
        outDataUrl  = GS::UniString ("data:image/png;base64,");
        outDataUrl += imgB64;
    }
    return true;
}

} // namespace NanoBanana
