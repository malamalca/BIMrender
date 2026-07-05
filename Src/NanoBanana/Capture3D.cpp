#include "APIEnvir.h"
#include "ACAPinc.h"
#include "ACAPI_Automate.h"
#include "ACAPI_Environment.h"

#include "NanoBanana/Capture3D.hpp"
#include "NanoBanana/Base64.hpp"

#include "Location.hpp"
#include "FileSystem.hpp"
#include "File.hpp"

#include <MessageLoopExecutor.hpp>
#include <FunctionRunnable.hpp>

#include <chrono>
#include <cstdio>
#include <string>

namespace NanoBanana {

// TEMPORARY diagnostics for the missing settings dialog; remove once fixed.
void NbDebugLog (const char* msg, long a)
{
    if (FILE* f = std::fopen ("/tmp/BIMrender_debug.log", "a")) {
        std::fprintf (f, msg, a);
        std::fprintf (f, "\n");
        std::fclose (f);
    }
}

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
// Deferred tasks via a module command (see Capture3D.hpp).
// All of this runs on the main thread: StartDeferredTask / FetchDeferredResult
// are called from the JS bridge (main thread on macOS, marshalled there by
// RunOnMainThread otherwise) and DeferredCommandHandler from the event loop.
// ---------------------------------------------------------------------------
static std::function<GS::UniString ()> deferredTask;
static GS::UniString                   deferredResult;
static bool                            deferredPending    = false;
static bool                            deferredDispatched = false;   // handler entered
static std::chrono::steady_clock::time_point deferredPostTime;

// Guard + arm shared by both Start variants. Returns an "ERROR: ..." text if
// a task is already active, otherwise arms the slot and returns empty.
static GS::UniString ArmDeferredTask (const std::function<GS::UniString ()>& task)
{
    if (deferredPending) {
        // One task at a time. A dispatched task may pend legitimately for
        // minutes (a modal dialog stays open as long as the user likes), so
        // it is always refused. Only a task whose handler never ran at all
        // is abandoned, after 30 seconds — so one lost dispatch can't block
        // every later operation. (If its handler still runs afterwards, it
        // executes the new task; the extra handler run is a no-op.)
        const auto pendingFor = std::chrono::steady_clock::now () - deferredPostTime;
        if (deferredDispatched || pendingFor < std::chrono::seconds (30))
            return GS::UniString ("ERROR: Another operation is still in progress. Try again in a moment.");
    }

    deferredPending    = true;
    deferredDispatched = false;
    deferredPostTime   = std::chrono::steady_clock::now ();
    deferredTask       = task;
    deferredResult.Clear ();
    return GS::EmptyUniString;
}

// Runs the armed task and publishes its result. Called by both dispatch paths.
static void RunDeferredTaskNow ()
{
    deferredDispatched = true;
    if (deferredTask != nullptr) {
        deferredResult = deferredTask ();
        deferredTask   = nullptr;
    }
    deferredPending = false;
}

GS::UniString StartDeferredTask (const std::function<GS::UniString ()>& task)
{
    const GS::UniString armErr = ArmDeferredTask (task);
    if (!armErr.IsEmpty ())
        return armErr;

    API_ModulID mdid = {};                 // our own 'MDID' resource values
    mdid.developerID = 1211329892;
    mdid.localID     = 439119418;

    const GSErrCode err = ACAPI_AddOnAddOnCommunication_CallFromEventLoop (
        &mdid, DeferredCmdID, DeferredCmdVersion, nullptr, false, nullptr);
    NbDebugLog ("StartDeferredTask: posted err=%ld", (long) err);

    if (err != NoError) {
        deferredPending = false;
        deferredTask    = nullptr;
        return GS::UniString::Printf ("ERROR: Could not schedule the operation (Archicad error %d).", (int) err);
    }
    return GS::EmptyUniString;
}

GS::UniString StartDeferredUiTask (const std::function<GS::UniString ()>& task)
{
    const GS::UniString armErr = ArmDeferredTask (task);
    if (!armErr.IsEmpty ())
        return armErr;

    NbDebugLog ("StartDeferredUiTask: posting to message loop");
    GS::MessageLoopExecutor ().Execute (
        GS::RunnableTask (new GS::FunctionRunnable ([] () {
            NbDebugLog ("DeferredUiTask: running");
            RunDeferredTaskNow ();
            NbDebugLog ("DeferredUiTask: done, result len=%ld", (long) deferredResult.GetLength ());
        })));
    return GS::EmptyUniString;
}

GS::UniString FetchDeferredResult ()
{
    if (deferredPending)
        return GS::UniString ("PENDING");

    GS::UniString result = deferredResult;
    deferredResult.Clear ();               // one-shot
    return result;
}

GSErrCode DeferredCommandHandler (GSHandle /*params*/, GSPtr /*resultData*/, bool /*silentMode*/)
{
    NbDebugLog ("DeferredCommandHandler: entered, haveTask=%ld", (long) (deferredTask != nullptr ? 1 : 0));
    RunDeferredTaskNow ();
    NbDebugLog ("DeferredCommandHandler: done, result len=%ld", (long) deferredResult.GetLength ());
    return NoError;
}

} // namespace NanoBanana
