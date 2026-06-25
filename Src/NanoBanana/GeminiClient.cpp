#include <string>

#include "APIEnvir.h"
#include "ACAPinc.h"

#include "NanoBanana/GeminiClient.hpp"
#include "NanoBanana/Base64.hpp"
#include "NanoBanana/Settings.hpp"

#include "HTTP/Client/ClientConnection.hpp"
#include "IBinaryChannelUtilities.hpp"
#include "IOBinProtocolXs.hpp"
#include "IChannelX.hpp"

#include "JSON/JDOMParser.hpp"
#include "JSON/Value.hpp"

namespace NanoBanana {

static const char* kHost  = "https://generativelanguage.googleapis.com";

// Base rules appended to every user prompt.
// Placed AFTER the user request so the model weights them most heavily.
static const char* kDefaultBaseRules =
    ". CRITICAL QUALITY RULES: The output must be a photorealistic architectural image, not a 3D render or illustration. "
    "Apply realistic PBR materials (concrete, glass, wood, metal, fabric) with correct roughness and reflectance. "
    "Use physically accurate lighting — natural sunlight or realistic interior illumination with proper shadows, "
    "ambient occlusion, and global illumination. Add photographic post-processing: natural colour grading, "
    "subtle lens characteristics, realistic exposure. Preserve all geometry exactly — never add, remove, move, "
    "or distort any architectural element. "
    "CRITICAL: Maintain maximum sharpness and detail throughout the entire image. Do not introduce blur, noise, "
    "grain, or compression artifacts. If a second reference image is provided, it is the original high-quality source — "
    "recover textures, edges, and fine details from it to counteract any quality loss in the primary input image. "
    "The output must be at least as sharp and detailed as the best reference image provided.";


// ---------------------------------------------------------------------------
// Turn a raw API error into a short, actionable message where we recognise it.
// ---------------------------------------------------------------------------
static GS::UniString FriendlyError (int status, const GS::UniString& raw)
{
    GS::UniString lower = raw;
    lower.SetToLowerCase ();

    const bool quota = lower.Contains ("quota") || lower.Contains ("free_tier") ||
                       lower.Contains ("limit: 0") || lower.Contains ("billing") ||
                       status == 429;
    if (quota) {
        return GS::UniString (
            "Image generation isn't available on this API key's free tier. "
            "Enable billing on your Google AI Studio / Cloud project, then try again.");
    }
    if (status == 400 && (lower.Contains ("api key") || lower.Contains ("api_key") || lower.Contains ("invalid"))) {
        return GS::UniString::Printf ("HTTP %d: ", status) + raw;
    }
    if (status == 404 && lower.Contains ("model"))
        return GS::UniString ("The configured model was not found. Check the model id in settings (⚙).");

    return raw;   // fall back to the raw API text
}

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
// Split a "data:<mime>;base64,<payload>" URL into mime + raw base64 payload.
// ---------------------------------------------------------------------------
static bool SplitDataUrl (const GS::UniString& dataUrl, std::string& mime, std::string& base64Payload)
{
    const std::string s (dataUrl.ToCStr (0, MaxUSize, CC_UTF8).Get ());
    if (s.compare (0, 5, "data:") != 0)
        return false;
    const size_t semi = s.find (';', 5);
    const size_t comma = s.find (',');
    if (semi == std::string::npos || comma == std::string::npos || comma < semi)
        return false;
    mime          = s.substr (5, semi - 5);
    base64Payload = s.substr (comma + 1);
    return !base64Payload.empty ();
}

// ---------------------------------------------------------------------------
// Find an inline image payload inside candidates[0].content.parts[].
// ---------------------------------------------------------------------------
// Read a string member from a JSON object (empty when absent / not a string).
static GS::UniString JStr (const JSON::ObjectValue& obj, const char* key)
{
    if (!obj.HasMember (key))
        return GS::EmptyUniString;
    const JSON::ValueRef v = obj.Get (key);
    if (v && v->IsString ())
        return JSON::StringValue::Cast (*v).Get ();
    return GS::EmptyUniString;
}

// Build a friendly "no image" message from the candidate's finishReason and any
// text the model returned instead of an image.
static GS::UniString NoImageReason (const GS::UniString& finishReason, const GS::UniString& modelText)
{
    GS::UniString m = "Gemini returned no image";
    if (!finishReason.IsEmpty () && finishReason != "STOP")
        m += GS::UniString (" (finishReason: ") + finishReason + ")";
    m += ".";
    if (!modelText.IsEmpty ())
        m += GS::UniString (" The model said: \"") + modelText + "\"";

    GS::UniString fr = finishReason;
    fr.SetToUpperCase ();
    if (fr.Contains ("SAFETY") || fr.Contains ("PROHIBITED") || fr.Contains ("BLOCK") || fr.Contains ("RECITATION"))
        m += " — this looks like a content/safety stop; try rephrasing or re-marking the area.";
    else
        m += " — region edits sometimes need a second attempt; try again or rephrase.";
    return m;
}

static bool ExtractImageData (const GS::UniString& responseJson, std::string& outBase64, GS::UniString& errMsg)
{
    try {
        JSON::JDOMStringParser parser;
        JSON::ValueRef root = parser.Parse (responseJson);
        if (!root || !root->IsObject ()) {
            errMsg = "Unexpected response from the image service.";
            return false;
        }
        const JSON::ObjectValue& rootObj = JSON::ObjectValue::Cast (*root);

        // Surface an API-reported error message if present.
        if (rootObj.HasMember ("error")) {
            const JSON::ValueRef errVal = rootObj.Get ("error");
            if (errVal && errVal->IsObject ()) {
                const GS::UniString msg = JStr (JSON::ObjectValue::Cast (*errVal), "message");
                if (!msg.IsEmpty ()) { errMsg = msg; return false; }
            }
        }

        // A blocked *prompt* comes back with promptFeedback.blockReason and no
        // usable candidate.
        if (rootObj.HasMember ("promptFeedback")) {
            const JSON::ValueRef pf = rootObj.Get ("promptFeedback");
            if (pf && pf->IsObject ()) {
                const GS::UniString br = JStr (JSON::ObjectValue::Cast (*pf), "blockReason");
                if (!br.IsEmpty ()) {
                    errMsg = GS::UniString ("Gemini blocked the request (blockReason: ") + br +
                             "). Rephrase the instruction or adjust the marked area.";
                    return false;
                }
            }
        }

        if (!rootObj.HasMember ("candidates")) {
            errMsg = "The image service returned no candidates.";
            return false;
        }
        const JSON::ValueRef candsVal = rootObj.Get ("candidates");
        if (!candsVal || !candsVal->IsArray ()) {
            errMsg = "The image service returned no candidates.";
            return false;
        }
        const JSON::ArrayValue& cands = JSON::ArrayValue::Cast (*candsVal);
        if (cands.GetSize () == 0) {
            errMsg = "The image service returned no candidates.";
            return false;
        }

        const JSON::ValueRef cand0 = cands.Get (0);
        if (!cand0 || !cand0->IsObject ())
            { errMsg = "Malformed candidate."; return false; }
        const JSON::ObjectValue& cand0Obj = JSON::ObjectValue::Cast (*cand0);

        // Why the model stopped (SAFETY, RECITATION, IMAGE_SAFETY, PROHIBITED_CONTENT, …).
        const GS::UniString finishReason = JStr (cand0Obj, "finishReason");

        // A candidate without content is a finishReason-only stop (blocked / declined).
        if (!cand0Obj.HasMember ("content"))
            { errMsg = NoImageReason (finishReason, GS::EmptyUniString); return false; }
        const JSON::ValueRef contentVal = cand0Obj.Get ("content");
        if (!contentVal || !contentVal->IsObject ())
            { errMsg = NoImageReason (finishReason, GS::EmptyUniString); return false; }
        const JSON::ObjectValue& contentObj = JSON::ObjectValue::Cast (*contentVal);

        if (!contentObj.HasMember ("parts"))
            { errMsg = NoImageReason (finishReason, GS::EmptyUniString); return false; }
        const JSON::ValueRef partsVal = contentObj.Get ("parts");
        if (!partsVal || !partsVal->IsArray ())
            { errMsg = NoImageReason (finishReason, GS::EmptyUniString); return false; }
        const JSON::ArrayValue& parts = JSON::ArrayValue::Cast (*partsVal);

        GS::UniString modelText;
        for (UIndex i = 0; i < parts.GetSize (); ++i) {
            const JSON::ValueRef partVal = parts.Get (i);
            if (!partVal || !partVal->IsObject ())
                continue;
            const JSON::ObjectValue& partObj = JSON::ObjectValue::Cast (*partVal);

            // Collect any text the model returned (an explanation when it declines).
            const GS::UniString txt = JStr (partObj, "text");
            if (!txt.IsEmpty ())
                modelText += txt;

            // Response uses "inlineData" (camelCase); accept "inline_data" too.
            const char* inlineKeys[2] = { "inlineData", "inline_data" };
            for (const char* key : inlineKeys) {
                if (!partObj.HasMember (key))
                    continue;
                const JSON::ValueRef inlineVal = partObj.Get (key);
                if (!inlineVal || !inlineVal->IsObject ())
                    continue;
                const JSON::ObjectValue& inlineObj = JSON::ObjectValue::Cast (*inlineVal);
                if (!inlineObj.HasMember ("data"))
                    continue;
                const JSON::ValueRef dataVal = inlineObj.Get ("data");
                if (dataVal && dataVal->IsString ()) {
                    outBase64 = std::string (JSON::StringValue::Cast (*dataVal).Get ().ToCStr (0, MaxUSize, CC_UTF8).Get ());
                    return true;
                }
            }
        }

        errMsg = NoImageReason (finishReason, modelText);
        return false;
    }
    catch (...) {
        errMsg = "Could not parse the image service response.";
        return false;
    }
}

// ---------------------------------------------------------------------------
// Helper: split a data URL and append an inlineData JSON fragment to body.
// ---------------------------------------------------------------------------
static void AppendInlineData (const GS::UniString& dataUrl, std::string& body)
{
    std::string mime, b64;
    if (SplitDataUrl (dataUrl, mime, b64)) {
        if (mime.empty ())
            mime = "image/png";
    } else {
        mime  = "image/png";
        // If it's not a data URL, assume raw base64.
        b64 = std::string (dataUrl.ToCStr (0, MaxUSize, CC_UTF8).Get ());
    }
    body += "{\"inlineData\":{\"mimeType\":\"";
    body += mime;
    body += "\",\"data\":\"";
    body += b64;
    body += "\"}}";
}

// ---------------------------------------------------------------------------
// RenderImage – the public entry point.
// ---------------------------------------------------------------------------
bool RenderImage (const GS::UniString& apiKey,
                  const GS::UniString& prompt,
                  const GS::UniString& inputDataUrl,
                  const GS::UniString& originalCapture,
                  const GS::Array<GS::UniString>& refImages,
                  const GS::UniString& promptHistory,
                  GS::UniString& outDataUrl,
                  GS::UniString& errMsg)
{
    if (apiKey.IsEmpty ()) {
        errMsg = "No API key configured.";
        return false;
    }

    std::string mime, base64Payload;
    if (!SplitDataUrl (inputDataUrl, mime, base64Payload)) {
        errMsg = "No captured image to send.";
        return false;
    }
    if (mime.empty ())
        mime = "image/png";

    const bool haveOriginal = !originalCapture.IsEmpty ();

    // Estimate total body size for reserve.
    GS::USize totalSize = static_cast<GS::USize> (base64Payload.size ());
    if (haveOriginal)
        totalSize += originalCapture.GetLength ();
    for (UIndex i = 0; i < refImages.GetSize (); ++i)
        totalSize += refImages[i].GetLength ();

    // Build combined prompt: user request first, then a note describing the
    // image order, then the quality rules (placed last so the model weights
    // them most heavily). The image order built below is:
    //   1. the working image            (always)
    //   2. the original capture         (only when haveOriginal)
    //   3..n user reference images      (refImages, in order)
    GS::UniString fullPrompt = prompt;
    if (haveOriginal)
        fullPrompt += " The second image is the original high-quality capture — "
                      "use it for detail and texture accuracy.";
    if (!refImages.IsEmpty ())
        fullPrompt += GS::UniString::Printf (
            " %d additional reference image(s) follow, numbered attachment 1 to %d in the order given. "
            "When the instruction refers to \"attachment N\" (e.g. \"attachment 3\"), it means the Nth of these reference images.",
            static_cast<int> (refImages.GetSize ()), static_cast<int> (refImages.GetSize ()));
    fullPrompt += kDefaultBaseRules;

    // A single user turn. Prior instructions (if any) are inlined as context
    // rather than sent as separate turns: the API expects alternating
    // user/model roles, and we have no model turns to interleave.
    GS::UniString turnText;
    if (!promptHistory.IsEmpty ()) {
        turnText  = "[Context — previously applied instructions, oldest first]\n";
        turnText += promptHistory;
        turnText += "\n[Current instruction]\n";
    }
    turnText += fullPrompt;

    std::string body;
    body.reserve (static_cast<size_t> (totalSize + 2048));
    body += "{\"contents\":[{\"role\":\"user\",\"parts\":[{\"text\":\"";
    body += JsonEscape (turnText);
    body += "\"}";     // close text part

    // Working image first (comma separator before each inlineData).
    body += ',';
    AppendInlineData (inputDataUrl, body);

    // Original capture (full-quality source reference) next, when supplied.
    if (haveOriginal) {
        body += ',';
        AppendInlineData (originalCapture, body);
    }

    // User reference images follow.
    for (UIndex i = 0; i < refImages.GetSize (); ++i) {
        body += ',';
        AppendInlineData (refImages[i], body);
    }

    body += "]}]}";

    // The API key travels in a header, not the URL query string, so it does not
    // leak into request logs / proxies.
    GS::UniString path ("/v1beta/models/");
    path += LoadModel ();
    path += ":generateContent";

    using namespace HTTP::Client;
    using namespace HTTP::MessageHeader;
    using namespace GS::IBinaryChannelUtilities;

    try {
        IO::URI::URI     connectionUrl (GS::UniString (kHost, CC_UTF8));
        ClientConnection conn (connectionUrl);
        conn.SetTimeout (120000);   // ms — image generation can take a while, but never hang forever
        conn.Connect ();

        Request request (Method::Id::Post, path);
        RequestHeaderFieldCollection& headers = request.GetRequestHeaderFieldCollection ();
        headers.Add (HeaderFieldName::ContentType, "application/json");
        headers.Add (GS::UniString ("x-goog-api-key"), apiKey);
        headers.Add (HeaderFieldName::UserAgent,   "arUtils-NanoBanana");

        // The body is handed to Send directly (do NOT pre-write a content channel).
        conn.Send (request, body.data (), static_cast<GS::USize> (body.size ()));
        conn.FinishSend ();

        Response response;
        GS::IChannelX channel (conn.BeginReceive (response), GS::GetNetworkByteOrderIProtocolX ());
        const GS::UniString respBody = ReadUniStringAsUTF8 (channel, NotTerminated);
        const int status = (int) response.GetStatusCode ();
        conn.FinishReceive ();
        conn.Close (false);

        if (status != 200) {
            // Try to extract a useful message from an error body.
            std::string ignore;
            GS::UniString apiMsg;
            if (ExtractImageData (respBody, ignore, apiMsg) == false && !apiMsg.IsEmpty ())
                errMsg = FriendlyError (status, apiMsg);
            else
                errMsg = GS::UniString::Printf ("Image service returned HTTP %d.", status);
            return false;
        }

        std::string outB64;
        if (!ExtractImageData (respBody, outB64, errMsg))
            return false;

        outDataUrl = GS::UniString ("data:image/png;base64,");
        outDataUrl += GS::UniString (outB64.c_str (), CC_UTF8);
        return true;
    }
    catch (GS::Exception& ex)   { errMsg = GS::UniString ("Network error: ") + ex.GetMessage (); }
    catch (...)                 { errMsg = "Unknown error contacting the image service."; }

    return false;
}

} // namespace NanoBanana
