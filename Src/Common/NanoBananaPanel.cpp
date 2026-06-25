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
#include <string>

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
    DG::FileDialog fileDialog (DG::FileDialog::Save);

    fileDialog.AddFilter (FTM::JPEGFileType, DG::FileDialog::SystemDefault);
    UIndex pngIdx = fileDialog.AddFilter (FTM::UnknownType, DG::FileDialog::DisplayExtensions);
    if (pngIdx != MaxUIndex)
        fileDialog.SetFilterText (pngIdx, "PNG Images (*.png)");

    GS::UniString defaultName = "ai_render" + ext;
    IO::Location defLoc (defaultName);
    fileDialog.SelectFile (defLoc, false);

    if (!fileDialog.Invoke ())
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
    browser.LoadHTML (LoadHtmlFromResource ());
    RegisterJavaScriptObject ();
}

void NanoBananaPanel::RegisterJavaScriptObject ()
{
    JS::Object* jsACAPI = new JS::Object ("ARUTILS");

    // Is the 3D window currently active?  -> bool
    jsACAPI->AddItem (new JS::Function ("Is3DActive", [] (GS::Ref<JS::Base>) -> GS::Ref<JS::Base> {
        return new JS::Value (NanoBanana::Is3DWindowActive ());
    }));

    // Capture the current 3D view -> "data:image/png;base64,..." (or "")
    jsACAPI->AddItem (new JS::Function ("Capture3D", [] (GS::Ref<JS::Base>) -> GS::Ref<JS::Base> {
        return new JS::Value (NanoBanana::CaptureCurrent3DAsDataUrl ());
    }));

    // Is the active provider configured enough to run?  -> bool
    jsACAPI->AddItem (new JS::Function ("HasApiKey", [] (GS::Ref<JS::Base>) -> GS::Ref<JS::Base> {
        return new JS::Value (NanoBanana::IsConfigured ());
    }));

    // Which backend is active?  -> "gemini" | "flux" | "local"
    // The page uses this to send the right region payload (marker vs. mask).
    jsACAPI->AddItem (new JS::Function ("GetProvider", [] (GS::Ref<JS::Base>) -> GS::Ref<JS::Base> {
        switch (NanoBanana::LoadProvider ()) {
            case NanoBanana::Provider::Flux:  return new JS::Value (GS::UniString ("flux"));
            case NanoBanana::Provider::Local: return new JS::Value (GS::UniString ("local"));
            default:                          return new JS::Value (GS::UniString ("gemini"));
        }
    }));

    // Open the settings dialog -> bool (true if the active provider is now configured)
    jsACAPI->AddItem (new JS::Function ("OpenSettings", [] (GS::Ref<JS::Base>) -> GS::Ref<JS::Base> {
        ShowNanoBananaSettingsDialog ();
        return new JS::Value (NanoBanana::IsConfigured ());
    }));

    // Render: params = [prompt, mainImage, originalCapture?, userRef1?, ...] -> new data URL, or "ERROR: <msg>"
    jsACAPI->AddItem (new JS::Function ("Render", [] (GS::Ref<JS::Base> params) -> GS::Ref<JS::Base> {
        GS::UniString prompt, dataUrl, originalCapture, promptHistory, mask;
        GS::Array<GS::UniString> refs;
        GetRenderParams (params, prompt, dataUrl, originalCapture, mask, refs, promptHistory);

        if (!NanoBanana::IsConfigured ())
            return new JS::Value (GS::UniString ("ERROR: Image backend not configured. Open settings first."));

        GS::UniString outDataUrl, errMsg;
        bool ok = false;
        switch (NanoBanana::LoadProvider ()) {
            case NanoBanana::Provider::Flux:
                ok = NanoBanana::FluxRenderImage (NanoBanana::LoadFluxKey (), NanoBanana::LoadFluxUrl (),
                                                  NanoBanana::LoadFluxModel (), prompt, dataUrl, mask, promptHistory,
                                                  outDataUrl, errMsg);
                break;
            case NanoBanana::Provider::Local:
                ok = NanoBanana::WebUIRenderImage (NanoBanana::LoadLocalUrl (), prompt, dataUrl, mask,
                                                   promptHistory, outDataUrl, errMsg);
                break;
            default:
                ok = NanoBanana::RenderImage (NanoBanana::LoadApiKey (), prompt, dataUrl, originalCapture,
                                              refs, promptHistory, outDataUrl, errMsg);
                break;
        }

        if (ok)
            return new JS::Value (outDataUrl);
        return new JS::Value (GS::UniString ("ERROR: ") + errMsg);
    }));

    // Save a data URL image to disk via native save dialog -> bool (true if saved)
    jsACAPI->AddItem (new JS::Function ("SaveImage", [] (GS::Ref<JS::Base> params) -> GS::Ref<JS::Base> {
        return new JS::Value (SaveDataUrlToFile (GetStringParam (params)));
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
