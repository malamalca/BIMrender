// *****************************************************************************
// Placing a rendered image into the project as a figure (picture element).
// *****************************************************************************

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "ACAPI_Database.h"

#include "NanoBanana/PlacePicture.hpp"
#include "NanoBanana/Base64.hpp"

#include <cstring>
#include <string>

namespace NanoBanana {

// ---------------------------------------------------------------------------
// Which windows accept a figure? Figures are 2D elements, so every drawing
// window and both layout types take one; the 3D model window does not.
// ---------------------------------------------------------------------------
static bool AcceptsPicture (API_WindowTypeID typeID)
{
    switch (typeID) {
        case APIWind_FloorPlanID:
        case APIWind_SectionID:
        case APIWind_ElevationID:
        case APIWind_InteriorElevationID:
        case APIWind_DetailID:
        case APIWind_WorksheetID:
        case APIWind_DocumentFrom3DID:
        case APIWind_LayoutID:
        case APIWind_MasterLayoutID:
            return true;
        default:
            return false;
    }
}

bool CanPlacePictureInCurrentWindow (GS::UniString* windowTitleOut)
{
    API_WindowInfo info = {};
    if (ACAPI_Window_GetCurrentWindow (&info) != NoError)
        return false;

    if (windowTitleOut != nullptr)
        *windowTitleOut = GS::UniString (info.title);

    return AcceptsPicture (info.typeID);
}

// ---------------------------------------------------------------------------
// Image header parsing.
// The pixel dimensions are only needed for the aspect ratio of the placed
// figure, so a minimal PNG/JPEG header read is enough — no image library, and
// no temp file to hand to GX::ImageBase::GetFileInfo.
// ---------------------------------------------------------------------------
static UInt32 BE32 (const unsigned char* p)
{
    return (static_cast<UInt32> (p[0]) << 24) | (static_cast<UInt32> (p[1]) << 16) |
           (static_cast<UInt32> (p[2]) << 8)  |  static_cast<UInt32> (p[3]);
}

// PNG: 8-byte signature, then the IHDR chunk whose data starts at offset 16
// with width and height as big-endian 32-bit values.
static bool PngSize (const std::string& bytes, UInt32& w, UInt32& h)
{
    static const unsigned char sig[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    if (bytes.size () < 24)
        return false;

    const unsigned char* p = reinterpret_cast<const unsigned char*> (bytes.data ());
    if (std::memcmp (p, sig, sizeof (sig)) != 0 || std::memcmp (p + 12, "IHDR", 4) != 0)
        return false;

    w = BE32 (p + 16);
    h = BE32 (p + 20);
    return w > 0 && h > 0;
}

// JPEG: walk the marker segments to the first start-of-frame (SOFn), which
// carries the size. SOF4 (DHT), SOF8 (JPG) and SOFC (DAC) are not frames.
static bool JpegSize (const std::string& bytes, UInt32& w, UInt32& h)
{
    const unsigned char* p = reinterpret_cast<const unsigned char*> (bytes.data ());
    const size_t         n = bytes.size ();
    if (n < 4 || p[0] != 0xFF || p[1] != 0xD8)
        return false;

    size_t i = 2;
    while (i + 3 < n) {
        if (p[i] != 0xFF) {                       // not on a marker: give up
            ++i;
            continue;
        }
        const unsigned char marker = p[i + 1];
        if (marker == 0xFF || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD9)) {
            i += 2;                               // padding / standalone marker
            continue;
        }

        const size_t segLen = (static_cast<size_t> (p[i + 2]) << 8) | static_cast<size_t> (p[i + 3]);
        const bool   isSOF  = (marker >= 0xC0 && marker <= 0xCF) &&
                              marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
        if (isSOF) {
            if (i + 9 >= n)
                return false;
            h = (static_cast<UInt32> (p[i + 5]) << 8) | static_cast<UInt32> (p[i + 6]);
            w = (static_cast<UInt32> (p[i + 7]) << 8) | static_cast<UInt32> (p[i + 8]);
            return w > 0 && h > 0;
        }
        if (segLen < 2)
            return false;
        i += 2 + segLen;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Split "data:<mime>;base64,<payload>" into its decoded bytes and format.
// ---------------------------------------------------------------------------
static bool DecodeDataUrl (const GS::UniString& dataUrl,
                           std::string&         bytesOut,
                           API_PictureFormat&   formatOut,
                           GS::UniString&       errOut)
{
    if (!dataUrl.BeginsWith (GS::UniString ("data:"))) {
        errOut = GS::UniString ("ERROR: No image to place.");
        return false;
    }

    const UIndex commaPos = dataUrl.FindFirst (',');
    if (commaPos == MaxUIndex) {
        errOut = GS::UniString ("ERROR: The image data is malformed.");
        return false;
    }

    GS::UniString mime;
    const UIndex semiPos = dataUrl.FindFirst (';', 5);
    if (semiPos != MaxUIndex && semiPos < commaPos)
        mime = dataUrl.GetSubstring (5, semiPos - 5);
    mime.SetToLowerCase ();

    if (mime.Contains ("png")) {
        formatOut = APIPictForm_PNG;
    } else if (mime.Contains ("jpeg") || mime.Contains ("jpg")) {
        formatOut = APIPictForm_JPEG;
    } else {
        // Archicad figures store a handful of standard formats; the render
        // backends return PNG or JPEG, so anything else is a bug elsewhere.
        errOut = GS::UniString ("ERROR: Only PNG and JPEG renders can be placed.");
        return false;
    }

    const GS::UniString b64Str = dataUrl.GetSubstring (commaPos + 1, MaxUSize);
    const std::string   utf8B64 (b64Str.ToCStr (0, MaxUSize, CC_UTF8).Get ());
    bytesOut = Base64Decode (utf8B64);
    if (bytesOut.empty ()) {
        errOut = GS::UniString ("ERROR: The image could not be decoded.");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Place the image as a figure, centred in the current zoom.
// ---------------------------------------------------------------------------
GS::UniString PlaceDataUrlAsPicture (const GS::UniString& dataUrl)
{
    API_WindowInfo windowInfo = {};
    if (ACAPI_Window_GetCurrentWindow (&windowInfo) != NoError)
        return GS::UniString ("ERROR: Could not identify the active window.");

    if (!AcceptsPicture (windowInfo.typeID))
        return GS::UniString ("ERROR: Pictures can only be placed in a 2D window. "
                              "Switch to the floor plan, a section, a detail or a layout, then press Place.");

    std::string       bytes;
    API_PictureFormat format = APIPictForm_PNG;
    GS::UniString     decodeErr;
    if (!DecodeDataUrl (dataUrl, bytes, format, decodeErr))
        return decodeErr;

    UInt32 pixW = 0, pixH = 0;
    const bool haveSize = (format == APIPictForm_PNG) ? PngSize (bytes, pixW, pixH)
                                                      : JpegSize (bytes, pixW, pixH);
    if (!haveSize) {                      // unreadable header: fall back to 4:3
        pixW = 4;
        pixH = 3;
    }

    // Size the figure against the visible area, so it lands at a usable size
    // whatever the drawing scale is: 60% of the zoom, shrunk further if that
    // would make it taller than 60% of the zoom.
    API_Box zoom = {};
    if (ACAPI_View_GetZoom (&zoom) != NoError)
        return GS::UniString ("ERROR: Could not read the current zoom of the window.");

    const double zoomW = zoom.xMax - zoom.xMin;
    const double zoomH = zoom.yMax - zoom.yMin;
    if (zoomW <= 0.0 || zoomH <= 0.0)
        return GS::UniString ("ERROR: Could not read the current zoom of the window.");

    const double aspect = static_cast<double> (pixH) / static_cast<double> (pixW);
    double       width  = zoomW * 0.6;
    double       height = width * aspect;
    if (height > zoomH * 0.6) {
        height = zoomH * 0.6;
        width  = height / aspect;
    }

    const double cx = (zoom.xMin + zoom.xMax) / 2.0;
    const double cy = (zoom.yMin + zoom.yMax) / 2.0;

    API_Element     element = {};
    API_ElementMemo memo    = {};

    element.header.type = API_PictureID;
    const GSErrCode defErr = ACAPI_Element_GetDefaults (&element, nullptr);
    if (defErr != NoError)
        return GS::UniString::Printf ("ERROR: Could not read the figure defaults (Archicad error %d).", (int) defErr);

    // usePixelSize would size the figure from the pixel count at the current
    // zoom; the destination box gives a predictable size instead (see the
    // API_PictureType table: xMax/yMax are the required inputs then).
    element.picture.usePixelSize  = false;
    element.picture.mirrored      = false;
    element.picture.transparent   = false;
    element.picture.rotAngle      = 0.0;
    element.picture.anchorPoint   = APIAnc_LB;
    element.picture.storageFormat = format;
    element.picture.destBox.xMin  = cx - width / 2.0;
    element.picture.destBox.xMax  = cx + width / 2.0;
    element.picture.destBox.yMin  = cy - height / 2.0;
    element.picture.destBox.yMax  = cy + height / 2.0;
    GS::ucscpy (element.picture.pictName, GS::UniString ("AI Render").ToUStr ().Get ());

    memo.pictHdl = BMAllocateHandle (static_cast<GSSize> (bytes.size ()), ALLOCATE_CLEAR, 0);
    if (memo.pictHdl == nullptr)
        return GS::UniString ("ERROR: Not enough memory to place the image.");
    std::memcpy (*memo.pictHdl, bytes.data (), bytes.size ());

    const GSErrCode createErr = ACAPI_CallUndoableCommand ("Place AI Render", [&] () -> GSErrCode {
        return ACAPI_Element_Create (&element, &memo);
    });

    ACAPI_DisposeElemMemoHdls (&memo);

    if (createErr != NoError)
        return GS::UniString::Printf ("ERROR: The picture could not be placed (Archicad error %d).", (int) createErr);

    return GS::UniString (windowInfo.title);
}

} // namespace NanoBanana
