// *****************************************************************************
// Writing a rendered image to disk (see SaveImageFile.hpp).
// *****************************************************************************

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "DG.h"
#include <DGFileDialog.hpp>

#include "NanoBanana/SaveImageFile.hpp"
#include "NanoBanana/Base64.hpp"
#include "NanoBanana/Capture3D.hpp"      // NbDebugLog

#include "FileTypeManager.hpp"
#include "Location.hpp"
#include "FileSystem.hpp"
#include "File.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

namespace NanoBanana {

const char* const SaveCancelled = "CANCELLED";

// ---------------------------------------------------------------------------
// Data URL -> bytes + file extension
// ---------------------------------------------------------------------------
// Splits a "data:<mime>;base64,<payload>" URL into the decoded bytes and the
// extension the mime type calls for. Returns false and fills `error` with an
// "ERROR: <reason>" text when the URL is not usable.
static bool DecodeImageDataUrl (const GS::UniString& dataUrl,
                                std::string&         outBytes,
                                GS::UniString&       outExt,
                                GS::UniString&       error)
{
    if (!dataUrl.BeginsWith (GS::UniString ("data:"))) {
        error = GS::UniString ("ERROR: No image to save.");
        return false;
    }

    const UIndex commaPos = dataUrl.FindFirst (',');
    if (commaPos == MaxUIndex) {
        error = GS::UniString ("ERROR: The image data is malformed.");
        return false;
    }

    // Extension from the mime type; PNG unless the header says otherwise.
    outExt = GS::UniString (".png");
    const UIndex semiPos = dataUrl.FindFirst (';', 5);
    if (semiPos != MaxUIndex && semiPos < commaPos) {
        GS::UniString mime = dataUrl.GetSubstring (5, semiPos - 5);
        mime.SetToLowerCase ();
        if (mime.Contains ("jpeg") || mime.Contains ("jpg"))
            outExt = GS::UniString (".jpg");
        else if (mime.Contains ("webp"))
            outExt = GS::UniString (".webp");
    }

    const GS::UniString b64Str = dataUrl.GetSubstring (commaPos + 1, MaxUSize);
    const std::string   utf8B64 (b64Str.ToCStr (0, MaxUSize, CC_UTF8).Get ());
    outBytes = Base64Decode (utf8B64);
    if (outBytes.empty ()) {
        error = GS::UniString ("ERROR: The image could not be decoded.");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// File writing
// ---------------------------------------------------------------------------
// Writes via IO::File so Unicode paths are handled correctly (a narrow
// std::ofstream would mangle non-ASCII paths on Windows). Returns an empty
// string on success, otherwise "ERROR: <reason>".
static GS::UniString WriteBytes (const IO::Location& loc, const std::string& bytes)
{
    NbDebugLog ("save: writing %ld bytes", (long) bytes.size ());

    IO::File outFile (loc, IO::File::Create);
    if (outFile.Open (IO::File::WriteEmptyMode) != NoError)
        return GS::UniString ("ERROR: The file could not be created. Check the folder permissions.");

    USize written = 0;
    const GSErrCode err = outFile.WriteBin (bytes.data (), static_cast<USize> (bytes.size ()), &written);
    outFile.Close ();
    if (err != NoError || written != static_cast<USize> (bytes.size ()))
        return GS::UniString ("ERROR: The image could not be written to disk.");

    return GS::EmptyUniString;
}

static GS::UniString PathOf (const IO::Location& loc)
{
    GS::UniString path;
    if (loc.ToPath (&path) != NoError)
        path = loc.ToDisplayText ();
    return path;
}

// ---------------------------------------------------------------------------
// Default folder: <Documents>/BIMrender, falling back to the temp folder.
// ---------------------------------------------------------------------------
static bool DefaultSaveFolder (IO::Location& folder)
{
    API_SpecFolderID specID = API_UserDocumentsFolderID;
    if (ACAPI_ProjectSettings_GetSpecFolder (&specID, &folder) != NoError) {
        specID = API_TemporaryFolderID;
        if (ACAPI_ProjectSettings_GetSpecFolder (&specID, &folder) != NoError)
            return false;
    }
    folder.AppendToLocal (IO::Name ("BIMrender"));
    IO::fileSystem.CreateFolderTree (folder);   // no-op if it already exists
    return true;
}

// The folder the Save dialog opens in. Starts at the default folder and then
// follows wherever the user last saved, for the lifetime of the Archicad
// session. It is deliberately not persisted: the add-on keeps a single
// preferences blob whose layout every version has to agree on, and this is not
// worth a format change.
static IO::Location lastSaveFolder;
static bool         haveLastSaveFolder = false;

// ---------------------------------------------------------------------------
// File dialog availability
// ---------------------------------------------------------------------------
bool IsFileDialogAvailable ()
{
#if defined (WINDOWS)
    return true;
#else
    // macOS: DG::FileDialog is a native NSSavePanel / NSOpenPanel, and the
    // panel's own modal run loop does not pump the JavaScript engine's message
    // pump. Opened from the message-loop task the browser bridge posts, it
    // therefore never services the page's GetAsyncResult poll and Archicad is
    // observed to hang (opened inline in the bridge callback it crashes
    // outright, which is why the Save dialog was dropped for an auto-save in
    // the first place).
    //
    // The Mac fix is an asynchronous sheet - [NSSavePanel
    // beginSheetModalForWindow:completionHandler:] - which returns to the event
    // loop immediately instead of nesting a run loop. That needs an
    // Objective-C++ translation unit (CMakeLists.txt globs *.mm on non-Windows
    // for it) and, because the completion handler runs after the deferred task
    // has already returned, the result has to be pushed into the page with
    // DG::Browser::ExecuteJS rather than picked up by the poll.
    //
    // Until that exists the Mac build auto-saves, and opening a custom image
    // falls back to the page's own <input type="file"> - the same mechanism the
    // attachment picker already uses, which works on both platforms. Set
    // BIMRENDER_SAVE_DIALOG=1 in the environment to try the DG dialogs on Mac
    // without rebuilding; it is off by default because a hang needs a force
    // quit.
    const char* env = std::getenv ("BIMRENDER_SAVE_DIALOG");
    return env != nullptr && env[0] == '1';
#endif
}

// ---------------------------------------------------------------------------
// File type filters
// ---------------------------------------------------------------------------
// The file types the dialogs filter on. Three rules, each of which cost a
// broken dialog to learn and all three checked against the IO module directly:
//
//   1. Every type in the group must be one this manager registered itself.
//      AddTypeToGroup refuses a type another manager owns - including the
//      DevKit's own FTM::JPEGFileType - and the group then does not contain
//      that extension, which is how the filters ended up missing entirely.
//   2. No mime strings. The manager keeps one hash-indexed mime table across
//      all types, so a second type declaring a mime that is already registered
//      fails to register at all and AddType hands back UnknownType. Filtering
//      on that is what made the dialog refuse a perfectly good PNG with "the
//      selected file is not a file".
//   3. A group, not one type carrying every extension, is what gives a dialog
//      a single "all of these" filter (as in the DevKit's
//      Goodie_Functions::Do_ParseJSON). FileType::AddExtension does split a
//      ';'-separated list, so one type can still cover jpg and jpeg.
//
// (The creator and MAC type are '    ' to match the DevKit examples, though 0
// works as well.)
//
// The manager owns the registrations and must outlive every dialog, so it
// lives in a static created once per session.
namespace {

struct ImageFileTypes {
    FTM::FileTypeManager manager;
    FTM::GroupID         group;   // all three formats at once, for the Open dialog
    FTM::TypeID          png;
    FTM::TypeID          jpeg;
    FTM::TypeID          webp;
    bool                 usable;  // did the registration actually take?

    ImageFileTypes () :
        manager ("BIMrender")
    {
        group = manager.AddGroup ("BIMrender Images");

        png = manager.AddType (FTM::FileType ("PNG Image", "png", '    ', '    ', 0), group);

        FTM::FileType jpegType ("JPEG Image", "jpg", '    ', '    ', 0);
        jpegType.AddExtension ("jpeg");
        jpeg = manager.AddType (jpegType, group);

        webp = manager.AddType (FTM::FileType ("WebP Image", "webp", '    ', '    ', 0), group);

        // A filter is a convenience; it must never be the reason a file cannot
        // be chosen. If the registration did not take, the dialogs go up
        // unfiltered rather than refusing files they should accept. Nothing is
        // lost by that: a save gets the right extension from WithExtension
        // either way, and an opened file is identified by its magic bytes
        // rather than by the filter.
        usable = png  != FTM::UnknownType &&
                 jpeg != FTM::UnknownType &&
                 webp != FTM::UnknownType &&
                 FTM::FileTypeManager::IsExtensionInGroup (GS::UniString ("png"),  group) &&
                 FTM::FileTypeManager::IsExtensionInGroup (GS::UniString ("jpg"),  group) &&
                 FTM::FileTypeManager::IsExtensionInGroup (GS::UniString ("jpeg"), group) &&
                 FTM::FileTypeManager::IsExtensionInGroup (GS::UniString ("webp"), group);
        NbDebugLog ("filetypes: usable=%ld", (long) (usable ? 1 : 0));
    }
};

// Deliberately leaked rather than a plain function-local static: a static
// object is destroyed when Archicad unloads the add-on, and FTM::FileTypeManager's
// destructor reaches into the IO module to unregister its types, which is not
// something to do at DLL teardown. One object per session is nothing.
const ImageFileTypes& GetImageFileTypes ()
{
    static ImageFileTypes* types = new ImageFileTypes ();
    return *types;
}

} // namespace

// Adds one filter and gives it a readable label. DisplayExtensions rather than
// SystemDefault on purpose: the DevKit's own example notes it is what forces
// the filter to be honoured on macOS.
static void AddFilter (DG::FileDialog& dialog, FTM::GeneralID id, const GS::UniString& text)
{
    const UIndex idx = dialog.AddFilter (id, DG::FileDialog::DisplayExtensions);
    if (idx != MaxUIndex)
        dialog.SetFilterText (idx, text);
}

// The Save dialog offers only the type the render actually is: the bytes are
// written through unchanged, so letting the user pick ".jpg" for PNG data would
// produce a file whose extension lies about its contents.
static void AddFilterForExtension (DG::FileDialog& dialog, const GS::UniString& ext)
{
    const ImageFileTypes& types = GetImageFileTypes ();
    if (!types.usable)
        return;

    if (ext == GS::UniString (".jpg"))
        AddFilter (dialog, types.jpeg, "JPEG Image (*.jpg, *.jpeg)");
    else if (ext == GS::UniString (".webp"))
        AddFilter (dialog, types.webp, "WebP Image (*.webp)");
    else
        AddFilter (dialog, types.png, "PNG Image (*.png)");
}

// Makes sure the name the user typed carries the extension the data calls for.
// The Save dialog appends it when the filter is honoured, but a name typed with
// a different extension - or with none, when the system hides them - comes back
// as typed.
static IO::Location WithExtension (const IO::Location& file, const GS::UniString& ext)
{
    IO::Name name;
    if (file.GetLastLocalName (&name) != NoError)
        return file;

    GS::UniString nameStr = name.ToString ();
    GS::UniString lower   = nameStr;
    lower.SetToLowerCase ();

    // A name that is nothing but the extension would make a dotfile (".png"),
    // which is hidden on both platforms - the user would not find what they
    // just saved. Give it the suggested stem instead.
    if (lower == ext)
        nameStr = GS::UniString ("ai_render") + ext;
    else if (lower.EndsWith (ext))
        return file;
    else
        nameStr += ext;

    IO::Location fixed = file;
    fixed.DeleteLastLocalName ();
    fixed.AppendToLocal (IO::Name (nameStr));
    return fixed;
}

// ---------------------------------------------------------------------------
// Save with dialog
// ---------------------------------------------------------------------------
GS::UniString SaveImageWithDialog (const GS::UniString& dataUrl)
{
    std::string   bytes;
    GS::UniString ext, error;
    if (!DecodeImageDataUrl (dataUrl, bytes, ext, error))
        return error;

    // Reopen where the last save went, unless that folder is gone by now - a
    // removed or unmounted folder would leave the dialog with nowhere to start.
    IO::Location folder;
    bool haveFolder = false;
    if (haveLastSaveFolder) {
        bool exists = false;
        if (IO::fileSystem.Contains (lastSaveFolder, &exists) == NoError && exists) {
            folder     = lastSaveFolder;
            haveFolder = true;
        } else {
            haveLastSaveFolder = false;
        }
    }
    if (!haveFolder && !DefaultSaveFolder (folder))
        return GS::UniString ("ERROR: Could not locate a folder to save into.");

    NbDebugLog ("save: decoded %ld bytes, opening dialog", (long) bytes.size ());

    DG::FileDialog fileDialog (DG::FileDialog::Save);
    AddFilterForExtension (fileDialog, ext);

    // Pre-fill the folder and the name. SelectFile gets a full location rather
    // than a bare file name: a location with no parent is rejected as invalid,
    // which leaves the dialog with no suggested name at all.
    IO::Location suggested = folder;
    suggested.AppendToLocal (IO::Name (GS::UniString ("ai_render") + ext));
    fileDialog.SetFolder (folder);
    fileDialog.SelectFile (suggested, false);

    const bool invoked = fileDialog.Invoke ();
    NbDebugLog ("save: dialog returned %ld", (long) (invoked ? 1 : 0));
    if (!invoked || fileDialog.GetSelectionCount () == 0)
        return GS::UniString (SaveCancelled);

    const IO::Location chosen = WithExtension (fileDialog.GetSelectedFile (0), ext);

    const GS::UniString writeErr = WriteBytes (chosen, bytes);
    if (!writeErr.IsEmpty ())
        return writeErr;

    // The next save opens where this one went.
    lastSaveFolder = chosen;
    lastSaveFolder.DeleteLastLocalName ();
    haveLastSaveFolder = true;

    NbDebugLog ("save: done");
    return PathOf (chosen);
}

// ---------------------------------------------------------------------------
// Save without dialog
// ---------------------------------------------------------------------------
// Reveals the saved file in the OS file browser (best-effort; failures are
// ignored). Only the no-dialog path does this: when the user picked the folder
// themselves, opening a window on it is noise.
static void RevealInFileBrowser (const IO::Location& file)
{
    GS::UniString path;
    if (file.ToPath (&path) != NoError)
        return;

    const std::string utf8 (path.ToCStr (0, MaxUSize, CC_UTF8).Get ());
    // Wrap the path in single quotes and escape any embedded quote so paths with
    // spaces or unicode survive the shell.
    std::string quoted = "'";
    for (char c : utf8)
        quoted += (c == '\'') ? std::string ("'\\''") : std::string (1, c);
    quoted += "'";

#if defined (WINDOWS)
    const std::string cmd = "explorer /select," + quoted;
#else
    const std::string cmd = "/usr/bin/open -R " + quoted;
#endif
    std::system (cmd.c_str ());
}

GS::UniString SaveImageToDefaultFolder (const GS::UniString& dataUrl)
{
    std::string   bytes;
    GS::UniString ext, error;
    if (!DecodeImageDataUrl (dataUrl, bytes, ext, error))
        return error;

    IO::Location folder;
    if (!DefaultSaveFolder (folder))
        return GS::UniString ("ERROR: Could not locate a folder to save into.");

    // Pick a non-colliding name: ai_render.png, ai_render_1.png, ...
    IO::Location outFileLoc;
    for (Int32 n = 0; ; ++n) {
        const GS::UniString name = (n == 0)
            ? GS::UniString ("ai_render") + ext
            : GS::UniString::Printf ("ai_render_%d", (int) n) + ext;
        outFileLoc = folder;
        outFileLoc.AppendToLocal (IO::Name (name));
        bool exists = false;
        if (IO::fileSystem.Contains (outFileLoc, &exists) != NoError || !exists)
            break;
    }

    const GS::UniString writeErr = WriteBytes (outFileLoc, bytes);
    if (!writeErr.IsEmpty ())
        return writeErr;

    RevealInFileBrowser (outFileLoc);
    NbDebugLog ("save: done");
    return PathOf (outFileLoc);
}

// ---------------------------------------------------------------------------
// Open an image file
// ---------------------------------------------------------------------------
// Refuse anything implausibly large before it is read into memory and then
// grown by a third again as base64 on its way through the JS bridge, where the
// page holds it a second time. 32 MB is already a ~100 MP JPEG, far past
// anything worth feeding an image model that normalizes its output to about
// 1 MP - and the page scales a source image down to 2048 px anyway.
static const UInt64 maxImageFileBytes = 32u * 1024u * 1024u;

// The mime type for the data URL, taken from the file's magic bytes rather than
// its name: the name is whatever the user typed, while the page and the image
// backends need the type to be right. Returns an empty string for anything that
// is not one of the three formats they handle.
static GS::UniString MimeFromMagicBytes (const std::string& bytes)
{
    const USize       n = static_cast<USize> (bytes.size ());
    const char* const b = bytes.data ();

    if (n >= 8 && std::memcmp (b, "\x89PNG\r\n\x1a\n", 8) == 0)
        return GS::UniString ("image/png");

    if (n >= 3 && std::memcmp (b, "\xFF\xD8\xFF", 3) == 0)
        return GS::UniString ("image/jpeg");

    // WebP: "RIFF" <4 byte little-endian size> "WEBP"
    if (n >= 12 && std::memcmp (b, "RIFF", 4) == 0 && std::memcmp (b + 8, "WEBP", 4) == 0)
        return GS::UniString ("image/webp");

    return GS::EmptyUniString;
}

// The folder the Open dialog starts in, remembered for the session like the
// save folder. They are kept apart on purpose: reference images and renders
// usually live in different places.
static IO::Location lastOpenFolder;
static bool         haveLastOpenFolder = false;

GS::UniString LoadImageWithDialog ()
{
    // Reopen where the last image came from, as long as that folder is still
    // there; otherwise start where saves go.
    IO::Location folder;
    bool haveFolder = false;
    if (haveLastOpenFolder) {
        bool exists = false;
        if (IO::fileSystem.Contains (lastOpenFolder, &exists) == NoError && exists) {
            folder     = lastOpenFolder;
            haveFolder = true;
        } else {
            haveLastOpenFolder = false;
        }
    }

    NbDebugLog ("open: opening dialog");

    DG::FileDialog fileDialog (DG::FileDialog::OpenFile);

    // Deliberately no type filter. The dialog checks the chosen file against the
    // selected filter by identifying its type across every registered manager,
    // and Archicad has already registered png / jpg itself, so a file that is
    // plainly a PNG was refused with "The selected file is not in Images" even
    // though the add-on's own group lists the extension. The bytes are checked
    // against their magic numbers below anyway, which is the check that matters.
    fileDialog.SetTitle (GS::UniString ("Select an image (PNG, JPEG or WebP)"));

    if (haveFolder || DefaultSaveFolder (folder))
        fileDialog.SetFolder (folder);

    const bool invoked = fileDialog.Invoke ();
    NbDebugLog ("open: dialog returned %ld", (long) (invoked ? 1 : 0));
    if (!invoked || fileDialog.GetSelectionCount () == 0)
        return GS::UniString (SaveCancelled);

    const IO::Location chosen = fileDialog.GetSelectedFile (0);

    IO::File inFile (chosen);
    const GSErrCode openErr = inFile.Open (IO::File::ReadMode);
    if (openErr != NoError) {
        // The code matters here: it is what tells a locked file apart from one
        // the dialog handed back as something other than a plain file.
        NbDebugLog ("open: IO::File::Open failed err=%ld", (long) openErr);
        return GS::UniString ("ERROR: That file could not be opened (error ") +
               GS::UniString::Printf ("%d", (int) openErr) + GS::UniString (").");
    }

    UInt64 length = 0;
    if (inFile.GetDataLength (&length) != NoError) {
        inFile.Close ();
        return GS::UniString ("ERROR: The size of that file could not be read.");
    }
    if (length == 0) {
        inFile.Close ();
        return GS::UniString ("ERROR: That file is empty.");
    }
    if (length > maxImageFileBytes) {
        inFile.Close ();
        return GS::UniString ("ERROR: That image is too large (the limit is 32 MB).");
    }

    std::string bytes (static_cast<size_t> (length), '\0');
    USize       read = 0;
    const GSErrCode err = inFile.ReadBin (&bytes[0], static_cast<USize> (length), &read);
    inFile.Close ();
    if (err != NoError || read != static_cast<USize> (length))
        return GS::UniString ("ERROR: That file could not be read.");

    const GS::UniString mime = MimeFromMagicBytes (bytes);
    if (mime.IsEmpty ())
        return GS::UniString ("ERROR: That is not a PNG, JPEG or WebP image.");

    NbDebugLog ("open: read %ld bytes", (long) bytes.size ());

    // The next open starts in the folder this file came from.
    lastOpenFolder = chosen;
    lastOpenFolder.DeleteLastLocalName ();
    haveLastOpenFolder = true;

    const std::string b64 = Base64Encode (bytes);
    GS::UniString dataUrl = GS::UniString ("data:") + mime + GS::UniString (";base64,");
    dataUrl.Append (b64.c_str (), static_cast<USize> (b64.size ()), CC_UTF8);
    return dataUrl;
}

} // namespace NanoBanana
