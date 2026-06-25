#include <string>
#include <thread>
#include <chrono>

#include "APIEnvir.h"
#include "ACAPinc.h"

#include "NanoBanana/FluxClient.hpp"
#include "NanoBanana/Base64.hpp"

#include "HTTP/Client/ClientConnection.hpp"
#include "IBinaryChannelUtilities.hpp"
#include "IOBinProtocolXs.hpp"
#include "IChannelX.hpp"

#include "JSON/JDOMParser.hpp"
#include "JSON/Value.hpp"

namespace NanoBanana {

// Poll the task at most this long before giving up (image edits take seconds).
static const int    kMaxPollSeconds  = 120;
static const int    kPollIntervalMs  = 1500;

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
// One HTTP exchange.  Reads the whole response body as raw bytes (works for
// both JSON and binary images).  Returns false only on a transport error.
// ---------------------------------------------------------------------------
struct HttpHeader { GS::UniString name; GS::UniString value; };

static bool HttpDo (HTTP::MessageHeader::Method::Id method,
                    const GS::UniString& absUrl,
                    const GS::Array<HttpHeader>& headers,
                    const std::string* body,
                    int& statusOut,
                    std::string& respOut,
                    GS::UniString& errMsg)
{
    using namespace HTTP::Client;
    using namespace HTTP::MessageHeader;

    try {
        IO::URI::URI uri (absUrl);
        GS::UniString base = uri.GetScheme () + "://" + uri.GetAuthority ();
        GS::UniString path = uri.GetPathAndQuery ();
        if (path.IsEmpty ())
            path = "/";

        IO::URI::URI      connUrl (base);
        ClientConnection  conn (connUrl);
        conn.SetTimeout (120000);
        conn.Connect ();

        Request request (method, path);
        RequestHeaderFieldCollection& hf = request.GetRequestHeaderFieldCollection ();
        for (UIndex i = 0; i < headers.GetSize (); ++i)
            hf.Add (headers[i].name, headers[i].value);

        if (body != nullptr)
            conn.Send (request, body->data (), static_cast<GS::USize> (body->size ()));
        else
            conn.Send (request);
        conn.FinishSend ();

        Response response;
        GS::IBinaryChannel& ch = conn.BeginReceive (response);
        std::string data;
        char buf[16384];
        for (;;) {
            GS::USize n = 0;
            try { n = ch.Read (buf, sizeof (buf), GS::ReadSomeMode); }
            catch (...) { break; }                       // end of stream
            if (n == 0)
                break;
            data.append (buf, n);
            if (data.size () > 64u * 1024u * 1024u)       // 64 MB safety cap
                break;
        }
        statusOut = static_cast<int> (response.GetStatusCode ());
        conn.FinishReceive ();
        conn.Close (false);
        respOut.swap (data);
        return true;
    }
    catch (GS::Exception& ex) { errMsg = GS::UniString ("Network error: ") + ex.GetMessage (); }
    catch (...)               { errMsg = "Unknown error contacting the Flux service."; }
    return false;
}

// ---------------------------------------------------------------------------
// FluxRenderImage – submit, poll, download.
// ---------------------------------------------------------------------------
bool FluxRenderImage (const GS::UniString& apiKey,
                      const GS::UniString& baseUrl,
                      const GS::UniString& model,
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

    // Normalise base URL (strip a trailing slash).
    GS::UniString base = baseUrl;
    base.Trim ();
    if (base.IsEmpty ())
        base = "https://api.bfl.ai";
    while (base.GetLength () > 0 && base.GetLast () == '/')
        base.DeleteLast ();

    // Compose the instruction: prior prompts as context, then the new request.
    GS::UniString turnText;
    if (!promptHistory.IsEmpty ()) {
        turnText  = "[Context — previously applied instructions, oldest first]\n";
        turnText += promptHistory;
        turnText += "\n[Current instruction]\n";
    }
    turnText += prompt;
    // For a masked Fill only the marked area changes, so "keep the geometry"
    // would fight edits like "put a sofa here"; keep just the realism cues.
    turnText += inpaint ? ". Produce a sharp, photorealistic result."
                        : ". Keep all architectural geometry exactly as in the input image; "
                          "produce a sharp, photorealistic result.";

    // ---- Submit -----------------------------------------------------------
    // A mask means a region edit: use the FLUX.1 Fill endpoint ("image" + "mask")
    // instead of the configured Kontext model ("input_image").
    std::string submitBody;
    submitBody.reserve (b64.size () + maskB64.size () + 1024);
    if (inpaint) {
        submitBody += "{\"prompt\":\"";
        submitBody += JsonEscape (turnText);
        submitBody += "\",\"image\":\"";
        submitBody += b64;
        submitBody += "\",\"mask\":\"";
        submitBody += maskB64;
        submitBody += "\",\"output_format\":\"png\"}";
    } else {
        submitBody += "{\"prompt\":\"";
        submitBody += JsonEscape (turnText);
        submitBody += "\",\"input_image\":\"";
        submitBody += b64;
        submitBody += "\",\"output_format\":\"png\"}";
    }

    GS::Array<HttpHeader> submitHeaders;
    submitHeaders.Push ({ GS::UniString ("Content-Type"), GS::UniString ("application/json") });
    submitHeaders.Push ({ GS::UniString ("accept"),       GS::UniString ("application/json") });
    submitHeaders.Push ({ GS::UniString ("User-Agent"),   GS::UniString ("arUtils-NanoBanana") });
    if (!apiKey.IsEmpty ())
        submitHeaders.Push ({ GS::UniString ("x-key"), apiKey });

    const GS::UniString submitUrl = inpaint ? (base + "/v1/flux-pro-1.0-fill")
                                            : (base + "/v1/" + model);

    int         status = 0;
    std::string resp;
    if (!HttpDo (HTTP::MessageHeader::Method::Id::Post, submitUrl, submitHeaders, &submitBody, status, resp, errMsg))
        return false;

    GS::UniString taskId, pollingUrl;
    try {
        JSON::JDOMStringParser parser;
        JSON::ValueRef root = parser.Parse (GS::UniString (resp.c_str (), CC_UTF8));
        if (root && root->IsObject ()) {
            const JSON::ObjectValue& o = JSON::ObjectValue::Cast (*root);
            taskId     = JGetStr (o, "id");
            pollingUrl = JGetStr (o, "polling_url");
        }
    } catch (...) { /* fall through to status check */ }

    if (status != 200 || taskId.IsEmpty ()) {
        if (status == 402)
            errMsg = "Flux rejected the request: out of credits on this account.";
        else if (status == 429)
            errMsg = "Flux is busy (too many active tasks). Try again shortly.";
        else if (status == 401 || status == 403)
            errMsg = "Flux authentication failed. Check the API key in settings (⚙).";
        else
            errMsg = GS::UniString::Printf ("Flux submit failed (HTTP %d): ", status)
                   + GS::UniString (resp.c_str (), CC_UTF8);
        return false;
    }

    if (pollingUrl.IsEmpty ())
        pollingUrl = base + "/v1/get_result?id=" + taskId;

    GS::Array<HttpHeader> pollHeaders;
    pollHeaders.Push ({ GS::UniString ("accept"), GS::UniString ("application/json") });
    if (!apiKey.IsEmpty ())
        pollHeaders.Push ({ GS::UniString ("x-key"), apiKey });

    // ---- Poll -------------------------------------------------------------
    const int maxAttempts = (kMaxPollSeconds * 1000) / kPollIntervalMs;
    GS::UniString sampleUrl;
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        std::this_thread::sleep_for (std::chrono::milliseconds (kPollIntervalMs));

        status = 0;
        resp.clear ();
        if (!HttpDo (HTTP::MessageHeader::Method::Id::Get, pollingUrl, pollHeaders, nullptr, status, resp, errMsg))
            return false;
        if (status != 200)
            continue;   // transient; keep trying until the timeout

        GS::UniString taskStatus;
        try {
            JSON::JDOMStringParser parser;
            JSON::ValueRef root = parser.Parse (GS::UniString (resp.c_str (), CC_UTF8));
            if (root && root->IsObject ()) {
                const JSON::ObjectValue& o = JSON::ObjectValue::Cast (*root);
                taskStatus = JGetStr (o, "status");
                if (o.HasMember ("result")) {
                    const JSON::ValueRef rv = o.Get ("result");
                    if (rv && rv->IsObject ())
                        sampleUrl = JGetStr (JSON::ObjectValue::Cast (*rv), "sample");
                }
            }
        } catch (...) { continue; }

        if (taskStatus == "Ready")
            break;
        if (taskStatus == "Pending" || taskStatus == "Processing" ||
            taskStatus == "Queued"  || taskStatus.IsEmpty ())
            continue;

        // Any other status is terminal (moderated / error / not found).
        errMsg = GS::UniString ("Flux could not produce an image: ") + taskStatus + ".";
        return false;
    }

    if (sampleUrl.IsEmpty ()) {
        errMsg = "Flux timed out before returning an image.";
        return false;
    }

    // ---- Download result --------------------------------------------------
    GS::Array<HttpHeader> noHeaders;   // signed delivery URL needs no auth
    status = 0;
    std::string imageBytes;
    if (!HttpDo (HTTP::MessageHeader::Method::Id::Get, sampleUrl, noHeaders, nullptr, status, imageBytes, errMsg))
        return false;
    if (status != 200 || imageBytes.empty ()) {
        errMsg = GS::UniString::Printf ("Failed to download the Flux result (HTTP %d).", status);
        return false;
    }

    outDataUrl  = GS::UniString ("data:image/png;base64,");
    outDataUrl += GS::UniString (Base64Encode (imageBytes).c_str (), CC_UTF8);
    return true;
}

} // namespace NanoBanana
