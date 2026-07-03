#include "APIEnvir.h"
#include "ACAPinc.h"
#include "ACAPI_Automate.h"
#include "ACAPI_Environment.h"

#include "NanoBanana/Capture3D.hpp"
#include "NanoBanana/Base64.hpp"

#include "Location.hpp"
#include "FileSystem.hpp"
#include "File.hpp"

#include <string>

namespace NanoBanana {

// ---------------------------------------------------------------------------
// Is the 3D model window the active one?
// ---------------------------------------------------------------------------
bool Is3DWindowActive ()
{
    API_WindowInfo info = {};
    if (ACAPI_Window_GetCurrentWindow (&info) != NoError)
        return false;

    return info.typeID == APIWind_3DModelID;
}

// ---------------------------------------------------------------------------
// Read a binary file fully into a std::string.
// Uses IO::File (not std::ifstream) so Unicode paths open correctly on Windows.
// ---------------------------------------------------------------------------
static bool ReadFileBytes (const IO::Location& loc, std::string& out)
{
    IO::File file (loc, IO::File::Fail);
    if (file.Open (IO::File::ReadMode) != NoError)
        return false;

    UInt64 length = 0;
    if (file.GetDataLength (&length) != NoError || length == 0) {
        file.Close ();
        return false;
    }

    out.resize (static_cast<size_t> (length));
    USize read = 0;
    const GSErrCode err = file.ReadBin (&out[0], static_cast<USize> (length), &read);
    file.Close ();

    return err == NoError && read == static_cast<USize> (length);
}

// ---------------------------------------------------------------------------
// Capture the current 3D view -> temp PNG -> base64 data URL.
// ---------------------------------------------------------------------------
GS::UniString CaptureCurrent3DAsDataUrl (GSErrCode* saveErrOut)
{
    if (saveErrOut != nullptr)
        *saveErrOut = NoError;

    if (!Is3DWindowActive ())
        return GS::UniString ();

    // Build the temporary output location.
    API_SpecFolderID specID = API_TemporaryFolderID;
    IO::Location     tempFolder;
    if (ACAPI_ProjectSettings_GetSpecFolder (&specID, &tempFolder) != NoError)
        return GS::UniString ();

    IO::Location outFile (tempFolder, IO::Name ("arUtils_nb_capture.png"));

    // Remove any leftover from a previous run so we don't read stale bytes.
    IO::fileSystem.Delete (outFile);

    API_FileSavePars fsp = {};
    fsp.fileTypeID = APIFType_PNGFile;
    fsp.file       = &outFile;

    API_SavePars_Picture pars = {};
#if defined (WINDOWS)
    pars.colorDepth            = APIColorDepth_TC24;  // True Color - 24 bit
#else
    pars.colorDepth            = APIColorDepth_MiC;   // Millions of Colors
#endif
    pars.dithered              = false;
    pars.view2D                = false;   // save the 3D view
    pars.crop                  = false;
    pars.keepSelectionHighlight = false;

    const GSErrCode err = ACAPI_ProjectOperation_Save (&fsp, &pars);
    if (err != NoError) {
        if (saveErrOut != nullptr)
            *saveErrOut = err;
        return GS::UniString ();
    }

    std::string pngBytes;
    const bool readOk = ReadFileBytes (outFile, pngBytes);

    // Best-effort cleanup of the temp file.
    IO::fileSystem.Delete (outFile);

    if (!readOk)
        return GS::UniString ();

    const std::string b64 = Base64Encode (pngBytes);
    GS::UniString dataUrl ("data:image/png;base64,");
    dataUrl += GS::UniString (b64.c_str (), CC_UTF8);
    return dataUrl;
}

// ---------------------------------------------------------------------------
// Asynchronous capture via a module command (see Capture3D.hpp).
// All of this runs on the main thread: StartAsyncCapture / FetchCaptureResult
// are called from the JS bridge (main thread on macOS, marshalled there by
// RunOnMainThread otherwise) and CaptureCommandHandler from the event loop.
// ---------------------------------------------------------------------------
static GS::UniString captureResult;
static bool          capturePending = false;

bool StartAsyncCapture ()
{
    if (capturePending)
        return true;                       // a capture is already in flight

    capturePending = true;
    captureResult.Clear ();

    API_ModulID mdid = {};                 // our own 'MDID' resource values
    mdid.developerID = 1211329892;
    mdid.localID     = 439119418;

    const GSErrCode err = ACAPI_AddOnAddOnCommunication_CallFromEventLoop (
        &mdid, CaptureCmdID, CaptureCmdVersion, nullptr, false, nullptr);

    if (err != NoError) {
        capturePending = false;
        return false;
    }
    return true;
}

GS::UniString FetchCaptureResult ()
{
    if (capturePending)
        return GS::UniString ("PENDING");

    GS::UniString result = captureResult;
    captureResult.Clear ();                // one-shot
    return result;
}

GSErrCode CaptureCommandHandler (GSHandle /*params*/, GSPtr /*resultData*/, bool /*silentMode*/)
{
    GSErrCode saveErr = NoError;
    captureResult = CaptureCurrent3DAsDataUrl (&saveErr);
    if (captureResult.IsEmpty ()) {
        // APIERR_REFUSEDCMD from the picture export in a clean command context
        // means Archicad itself refuses to save — the documented case is the
        // Demo version, which cannot save anything.
        if (saveErr == APIERR_REFUSEDCMD)
            captureResult = "ERROR: Archicad refused to export the view. "
                            "The Demo version cannot save; a full license is required.";
        else
            captureResult = "ERROR: Could not capture. Make sure the 3D window is active.";
    }
    capturePending = false;
    return NoError;
}

} // namespace NanoBanana
