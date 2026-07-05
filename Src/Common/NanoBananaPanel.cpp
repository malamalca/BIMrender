// *****************************************************************************
// NanoBanana AI render panel - shared controls and logic hosting a DG::Browser
// and bridging it to the Archicad API (3D capture + Nano Banana image
// rendering).  Used by both NanoBananaDialog (modal) and NanoBananaPalette
// (modeless).
// *****************************************************************************

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "DG.h"
#include <ResourceIds.hpp>

#include "Common/NanoBananaPanel.hpp"
#include "Dialogs/dlgNanoBananaSettings.hpp"
#include "NanoBanana/Base64.hpp"
#include "NanoBanana/Capture3D.hpp"
#include "NanoBanana/GeminiClient.hpp"
#include "NanoBanana/FluxClient.hpp"
#include "NanoBanana/WebUIClient.hpp"
#include "NanoBanana/Settings.hpp"

#include "JSValues.hpp"

#include <DGFileDialog.hpp>
#include <Location.hpp>
#include <File.hpp>
#include <MessageLoopExecutor.hpp>
#include <FunctionRunnable.hpp>
#include <functional>
#include <string>
#include <thread>

// ---------------------------------------------------------------------------
// Load the embedded HTML page from the 'DATA' resource.
// ---------------------------------------------------------------------------
static GS::UniString LoadHtmlFromResource ()
{
    GS::UniString resourceData;
    GSHandle data = RSLoadResource ('DATA', ACAPI_GetOwnResModule (), ID_DATA_NANOBANANA_HTML);
    if (data != nullptr) {
        const GSSize handleSize = BMhGetSize (data);
        resourceData.Append (*data, handleSize, CC_UTF8);   // the page is UTF-8 (emoji, …, ‹›)
        BMhKill (&data);
    }
    return resourceData;
}

// ---------------------------------------------------------------------------
// JS bridge helpers
// ---------------------------------------------------------------------------
static GS::UniString GetStringParam (GS::Ref<JS::Base> jsVariable)
{
    GS::Ref<JS::Value> jsValue = GS::DynamicCast<JS::Value> (jsVariable);
    if (jsValue != nullptr && jsValue->GetType () == JS::Value::STRING)
        return jsValue->GetString ();
    return GS::EmptyUniString;
}

// The Render call receives a fixed-position array:
//   [0] prompt
//   [1] working image data URL
//   [2] original capture data URL  ("" when same as the working image)
//   [3] prompt history             ("" when none)
//   [4] region mask data URL       ("" when no region; used by the Local backend)
//   [5..] user reference image data URLs
static void GetRenderParams (GS::Ref<JS::Base> params,
                             GS::UniString& prompt,
                             GS::UniString& dataUrl,
                             GS::UniString& originalCapture,
                             GS::UniString& mask,
                             GS::Array<GS::UniString>& refs,
                             GS::UniString& promptHistory)
{
    refs.Clear ();
    originalCapture.Clear ();
    promptHistory.Clear ();
    mask.Clear ();
    GS::Ref<JS::Array> arr = GS::DynamicCast<JS::Array> (params);
    if (arr == nullptr)
        return;
    const GS::Array<GS::Ref<JS::Base>>& items = arr->GetItemArray ();
    if (items.GetSize () >= 1) prompt          = GetStringParam (items[0]);
    if (items.GetSize () >= 2) dataUrl         = GetStringParam (items[1]);
    if (items.GetSize () >= 3) originalCapture = GetStringParam (items[2]);
    if (items.GetSize () >= 4) promptHistory   = GetStringParam (items[3]);
    if (items.GetSize () >= 5) mask            = GetStringParam (items[4]);

    for (UIndex i = 5; i < items.GetSize (); ++i)
        refs.Push (GetStringParam (items[i]));
}

static bool SaveDataUrlToFile (const GS::UniString& dataUrl)
{
    // Check "data:" prefix.
    if (dataUrl.GetLength () < 5)
        return false;
    GS::UniString prefix = dataUrl.GetSubstring (0, 5);
    if (prefix.Compare (GS::UniString ("data:")) != GS::UniString::Equal)
        return false;

    // Find comma separating header from base64 payload.
    const UIndex commaPos = dataUrl.FindFirst (',');
    if (commaPos == MaxUIndex)
        return false;
    GS::UniString b64Str = dataUrl.GetSubstring (commaPos + 1, MaxUSize);

    // Determine file extension from mime type.
    GS::UniString ext (".png");
    const UIndex semiPos = dataUrl.FindFirst (';', 5);
    if (semiPos != MaxUIndex) {
        GS::UniString mime = dataUrl.GetSubstring (5, semiPos - 5);
        GS::UniString mimeLower = mime;
        mimeLower.SetToLowerCase ();
        if (mimeLower.Contains ("jpeg") || mimeLower.Contains ("jpg"))
            ext = ".jpg";
        else if (mimeLower.Contains ("webp"))
            ext = ".webp";
    }

    // Decode base64 to binary.
    const std::string utf8B64 (b64Str.ToCStr (0, MaxUSize, CC_UTF8).Get ());
    const std::string decoded = NanoBanana::Base64Decode (utf8B64);

    // Show save dialog using DG::FileDialog.
    NanoBanana::NbDebugLog ("save: decoded %ld bytes, opening dialog", (long) decoded.size ());
    DG::FileDialog fileDialog (DG::FileDialog::Save);

    fileDialog.AddFilter (FTM::JPEGFileType, DG::FileDialog::SystemDefault);
    UIndex pngIdx = fileDialog.AddFilter (FTM::UnknownType, DG::FileDialog::DisplayExtensions);
    if (pngIdx != MaxUIndex)
        fileDialog.SetFilterText (pngIdx, "PNG Images (*.png)");

    GS::UniString defaultName = "ai_render" + ext;
    IO::Location defLoc (defaultName);
    fileDialog.SelectFile (defLoc, false);

    const bool invoked = fileDialog.Invoke ();
    NanoBanana::NbDebugLog ("save: dialog returned %ld", (long) (invoked ? 1 : 0));
    if (!invoked)
        return false;

    const IO::Location& selectedFile = fileDialog.GetSelectedFile (0);

    // Write via IO::File so Unicode paths are handled correctly (a narrow
    // std::ofstream would mangle non-ASCII paths on Windows).
    IO::File outFile (selectedFile, IO::File::Create);
    if (outFile.Open (IO::File::WriteEmptyMode) != NoError)
        return false;

    USize written = 0;
    const GSErrCode err = outFile.WriteBin (decoded.data (), static_cast<USize> (decoded.size ()), &written);
    outFile.Close ();
    return err == NoError && written == static_cast<USize> (decoded.size ());
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------
NanoBananaPanel::NanoBananaPanel (DG::Dialog& panel, short /*resId*/) :
    panel_   (panel),
    browser  (panel, BrowserId)
{
    panel_.Attach (*this);
}

NanoBananaPanel::~NanoBananaPanel ()
{
    panel_.Detach (*this);
}

// ---------------------------------------------------------------------------
// Browser init + JS object registration
// ---------------------------------------------------------------------------
void NanoBananaPanel::InitBrowserControl ()
{
    // Register the JS bridge BEFORE loading the page. On macOS (WKWebView) the
    // browser injects registered JS objects into the page at load time, so a
    // registration that happens after LoadHTML never reaches the document and
    // window.ARUTILS stays undefined (leaving the Capture button permanently
    // disabled). On Windows (WebView2) the order is not significant, but doing
    // it first is correct on both platforms.
    RegisterJavaScriptObject ();
    browser.LoadHTML (LoadHtmlFromResource ());
}

// ---------------------------------------------------------------------------
// ACAPI and DG calls are only valid on the main (message-loop) thread, but
// which thread the browser delivers JS callbacks on is a CEF implementation
// detail (observed: main thread on macOS, worker thread on Windows). Every
// handler below therefore marshals its Archicad-touching work through
// RunOnMainThread, which is a plain inline call when already on the main
// thread. The long-running network calls stay on the calling thread so the
// UI keeps responding while a render is in flight.
// ---------------------------------------------------------------------------
static std::thread::id mainThreadId;

static void RunOnMainThread (const std::function<void ()>& fn)
{
    if (std::this_thread::get_id () == mainThreadId) {
        fn ();
        return;
    }
    GS::MessageLoopExecutor ().ExecuteAndWait (GS::RunnableTask (new GS::FunctionRunnable (fn)));
}

void NanoBananaPanel::RegisterJavaScriptObject ()
{
    mainThreadId = std::this_thread::get_id ();   // registration runs on the main thread

    JS::Object* jsACAPI = new JS::Object ("ARUTILS");

    // Booleans are returned as the strings "true"/"false": on macOS the CEF
    // bridge delivers a JS::Value(bool) to the page as an empty string, so a
    // native `true` arrives falsy and e.g. the Capture button never enables.
    // Strings cross the bridge intact on both platforms; the page parses them
    // with asBool().
    auto boolValue = [] (bool b) -> GS::Ref<JS::Base> {
        return new JS::Value (GS::UniString (b ? "true" : "false"));
    };

    // Is the 3D window currently active?  -> "true" | "false"
    jsACAPI->AddItem (new JS::Function ("Is3DActive", [boolValue] (GS::Ref<JS::Base>) -> GS::Ref<JS::Base> {
        bool active = false;
        RunOnMainThread ([&] { active = NanoBanana::Is3DWindowActive (); });
        return boolValue (active);
    }));

    // Start capturing the current 3D view -> "true" | "false" (started?).
    // The capture runs as a deferred task from the event loop, because
    // ACAPI_ProjectOperation_Save is refused inside this bridge callback
    // (APIERR_REFUSEDCMD). The page polls GetAsyncResult for the outcome.
    // Start* results: "true" when the deferred task was scheduled, otherwise
    // an "ERROR: <reason>" text the page shows in the status line.
    jsACAPI->AddItem (new JS::Function ("Capture3D", [] (GS::Ref<JS::Base>) -> GS::Ref<JS::Base> {
        GS::UniString startErr;
        RunOnMainThread ([&] {
            startErr = NanoBanana::StartDeferredTask ([] () -> GS::UniString {
                GSErrCode saveErr = NoError;
                GS::UniString dataUrl = NanoBanana::CaptureCurrent3DAsDataUrl (&saveErr);
                if (!dataUrl.IsEmpty ())
                    return dataUrl;
                // APIERR_REFUSEDCMD from the picture export in command context
                // means Archicad itself refuses to save — the documented case
                // is the Demo version, which cannot save anything.
                if (saveErr == APIERR_REFUSEDCMD)
                    return GS::UniString ("ERROR: Archicad refused to export the view. "
                                          "The Demo version cannot save; a full license is required.");
                return GS::UniString ("ERROR: Could not capture. Make sure the 3D window is active.");
            });
        });
        return new JS::Value (startErr.IsEmpty () ? GS::UniString ("true") : startErr);
    }));

    // Poll the pending deferred task (capture / save / settings)
    // -> "PENDING" | task result | "" (no task active)
    jsACAPI->AddItem (new JS::Function ("GetAsyncResult", [] (GS::Ref<JS::Base>) -> GS::Ref<JS::Base> {
        GS::UniString result;
        RunOnMainThread ([&] { result = NanoBanana::FetchDeferredResult (); });
        return new JS::Value (result);
    }));

    // Is the active provider configured enough to run?  -> "true" | "false"
    jsACAPI->AddItem (new JS::Function ("HasApiKey", [boolValue] (GS::Ref<JS::Base>) -> GS::Ref<JS::Base> {
        bool configured = false;
        RunOnMainThread ([&] { configured = NanoBanana::IsConfigured (); });
        return boolValue (configured);
    }));

    // Which backend is active?  -> "gemini" | "flux" | "local"
    // The page uses this to send the right region payload (marker vs. mask).
    jsACAPI->AddItem (new JS::Function ("GetProvider", [] (GS::Ref<JS::Base>) -> GS::Ref<JS::Base> {
        NanoBanana::Provider provider = NanoBanana::Provider::Gemini;
        RunOnMainThread ([&] { provider = NanoBanana::LoadProvider (); });
        switch (provider) {
            case NanoBanana::Provider::Flux:  return new JS::Value (GS::UniString ("flux"));
            case NanoBanana::Provider::Local: return new JS::Value (GS::UniString ("local"));
            default:                          return new JS::Value (GS::UniString ("gemini"));
        }
    }));

    // Open the settings dialog -> "true" | "ERROR: <reason>" (started?).
    // Runs as a message-loop UI task: opened inline here it crashes (nested
    // event loop in a blocked CEF IPC call), and opened inside the module
    // command it never becomes visible on macOS. The page polls
    // GetAsyncResult; the task result is "true" when the active provider is
    // configured.
    jsACAPI->AddItem (new JS::Function ("OpenSettings", [] (GS::Ref<JS::Base>) -> GS::Ref<JS::Base> {
        GS::UniString startErr;
        RunOnMainThread ([&] {
            startErr = NanoBanana::StartDeferredUiTask ([] () -> GS::UniString {
                NanoBanana::NbDebugLog ("settings task: invoking dialog");
                const bool ok = ShowNanoBananaSettingsDialog ();
                NanoBanana::NbDebugLog ("settings task: dialog returned %ld", (long) (ok ? 1 : 0));
                return GS::UniString (NanoBanana::IsConfigured () ? "true" : "false");
            });
        });
        return new JS::Value (startErr.IsEmpty () ? GS::UniString ("true") : startErr);
    }));

    // Render: params = [prompt, mainImage, originalCapture?, userRef1?, ...] -> new data URL, or "ERROR: <msg>"
    jsACAPI->AddItem (new JS::Function ("Render", [] (GS::Ref<JS::Base> params) -> GS::Ref<JS::Base> {
        GS::UniString prompt, dataUrl, originalCapture, promptHistory, mask;
        GS::Array<GS::UniString> refs;
        GetRenderParams (params, prompt, dataUrl, originalCapture, mask, refs, promptHistory);

        // Snapshot all settings on the main thread; the network call below
        // then runs without touching the Archicad preferences.
        bool configured = false;
        NanoBanana::Provider provider = NanoBanana::Provider::Gemini;
        GS::UniString geminiKey, geminiModel, fluxKey, fluxUrl, fluxModel, localUrl;
        RunOnMainThread ([&] {
            configured  = NanoBanana::IsConfigured ();
            provider    = NanoBanana::LoadProvider ();
            geminiKey   = NanoBanana::LoadApiKey ();
            geminiModel = NanoBanana::LoadModel ();
            fluxKey     = NanoBanana::LoadFluxKey ();
            fluxUrl     = NanoBanana::LoadFluxUrl ();
            fluxModel   = NanoBanana::LoadFluxModel ();
            localUrl    = NanoBanana::LoadLocalUrl ();
        });

        if (!configured)
            return new JS::Value (GS::UniString ("ERROR: Image backend not configured. Open settings first."));

        GS::UniString outDataUrl, errMsg;
        bool ok = false;
        switch (provider) {
            case NanoBanana::Provider::Flux:
                ok = NanoBanana::FluxRenderImage (fluxKey, fluxUrl, fluxModel, prompt, dataUrl, mask,
                                                  promptHistory, outDataUrl, errMsg);
                break;
            case NanoBanana::Provider::Local:
                ok = NanoBanana::WebUIRenderImage (localUrl, prompt, dataUrl, mask,
                                                   promptHistory, outDataUrl, errMsg);
                break;
            default:
                ok = NanoBanana::RenderImage (geminiKey, geminiModel, prompt, dataUrl, originalCapture,
                                              refs, promptHistory, outDataUrl, errMsg);
                break;
        }

        if (ok)
            return new JS::Value (outDataUrl);
        return new JS::Value (GS::UniString ("ERROR: ") + errMsg);
    }));

    // Save a data URL image to disk -> "true" | "ERROR: <reason>" (started?).
    // Runs as a message-loop UI task for the same reason as OpenSettings
    // (native modal save dialog). The page polls GetAsyncResult; the task
    // result is "true" when saved.
    jsACAPI->AddItem (new JS::Function ("SaveImage", [] (GS::Ref<JS::Base> params) -> GS::Ref<JS::Base> {
        const GS::UniString dataUrl = GetStringParam (params);
        GS::UniString startErr;
        RunOnMainThread ([&] {
            startErr = NanoBanana::StartDeferredUiTask ([dataUrl] () -> GS::UniString {
                return GS::UniString (SaveDataUrlToFile (dataUrl) ? "true" : "false");
            });
        });
        return new JS::Value (startErr.IsEmpty () ? GS::UniString ("true") : startErr);
    }));

    // Expand a brief prompt into a detailed one via a Gemini text model.
    // Uses the Gemini key regardless of the active provider -> new prompt, or "ERROR: <msg>"
    jsACAPI->AddItem (new JS::Function ("EnhancePrompt", [] (GS::Ref<JS::Base> params) -> GS::Ref<JS::Base> {
        GS::UniString apiKey;
        RunOnMainThread ([&] { apiKey = NanoBanana::LoadApiKey (); });

        GS::UniString outText, errMsg;
        if (NanoBanana::EnhancePromptText (apiKey, GetStringParam (params), outText, errMsg))
            return new JS::Value (outText);
        return new JS::Value (GS::UniString ("ERROR: ") + errMsg);
    }));

    browser.RegisterAsynchJSObject (jsACAPI);
}

// ---------------------------------------------------------------------------
// Panel events
// ---------------------------------------------------------------------------
void NanoBananaPanel::PanelOpened (const DG::PanelOpenEvent&)
{
    InitBrowserControl ();
}

void NanoBananaPanel::PanelResized (const DG::PanelResizeEvent& ev)
{
    const short dh = ev.GetHorizontalChange ();
    const short dv = ev.GetVerticalChange ();

    panel_.BeginMoveResizeItems ();
    browser.Resize (dh, dv);          // browser grows with the panel
    panel_.EndMoveResizeItems ();
}
