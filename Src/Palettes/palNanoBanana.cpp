// *****************************************************************************
// NanoBanana AI render MODELESS PALETTE
//
// Thin wrapper - palette lifecycle only.  All controls and shared logic live
// in NanoBananaPanel (Common/).  Used in release builds; DEBUG builds use the
// modal NanoBananaDialog.
// *****************************************************************************

#include "APIEnvir.h"
#include "ACAPinc.h"
#include <ResourceIds.hpp>

#include "Palettes/palNanoBanana.hpp"

// ---------------------------------------------------------------------------
// Static data
// ---------------------------------------------------------------------------
GS::Ref<NanoBananaPalette> NanoBananaPalette::instance;

// ---------------------------------------------------------------------------
// Identity helpers
// ---------------------------------------------------------------------------
const GS::Guid& NanoBananaPalette::PaletteGuid ()
{
    static GS::Guid guid ("F1CF73DE-6510-4EB8-8893-5E30E333C6F0");
    return guid;
}

Int32 NanoBananaPalette::PaletteRefId ()
{
    static Int32 refId (GS::CalculateHashValue (PaletteGuid ()));
    return refId;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
NanoBananaPalette::NanoBananaPalette () :
    DG::Palette     (ACAPI_GetOwnResModule (), ID_PALETTE_NANOBANANA, ACAPI_GetOwnResModule ()),
    NanoBananaPanel (*this, ID_PALETTE_NANOBANANA)
{
    BeginEventProcessing ();
}

NanoBananaPalette::~NanoBananaPalette ()
{
    EndEventProcessing ();
}

// ---------------------------------------------------------------------------
// Close behaviour (title-bar close box) - just hide; do not destroy
// ---------------------------------------------------------------------------
void NanoBananaPalette::PanelCloseRequested (const DG::PanelCloseRequestEvent&, bool* accepted)
{
    Hide ();
    *accepted = true;
}

// ---------------------------------------------------------------------------
// Singleton management
// ---------------------------------------------------------------------------
bool NanoBananaPalette::HasInstance ()
{
    return instance != nullptr;
}

void NanoBananaPalette::CreateInstance ()
{
    DBASSERT (!HasInstance ());
    instance = new NanoBananaPalette ();
    ACAPI_KeepInMemory (true);
}

NanoBananaPalette& NanoBananaPalette::GetInstance ()
{
    DBASSERT (HasInstance ());
    return *instance;
}

void NanoBananaPalette::DestroyInstance ()
{
    instance = nullptr;
}

// ---------------------------------------------------------------------------
// Show / Hide
// ---------------------------------------------------------------------------
void NanoBananaPalette::Show ()
{
    DG::Palette::Show ();
    SetMenuItemCheckedState (true);
}

void NanoBananaPalette::Hide ()
{
    DG::Palette::Hide ();
    SetMenuItemCheckedState (false);
}

// ---------------------------------------------------------------------------
// Archicad palette callback
// ---------------------------------------------------------------------------
GSErrCode NanoBananaPalette::PaletteControlCallBack (Int32, API_PaletteMessageID messageID, GS::IntPtr param)
{
    // Archicad brackets view switches (and similar operations) with
    // HidePalette_Begin / HidePalette_End.  Remember whether the palette was
    // actually open when the hide began, so the End handler only restores it
    // when the user had it open — not after the user deliberately closed it.
    // (Pattern from the SDK DG_Test/OwnerDrawnListBoxPalette example.)
    static bool paletteOpenAtClose = false;

    switch (messageID) {
        case APIPalMsg_OpenPalette:
            if (!HasInstance ())
                CreateInstance ();
            paletteOpenAtClose = true;
            GetInstance ().Show ();
            break;

        case APIPalMsg_ClosePalette:
        case APIPalMsg_HidePalette_Begin:
            if (HasInstance () && GetInstance ().IsVisible ()) {
                paletteOpenAtClose = true;
                GetInstance ().Hide ();
            } else {
                paletteOpenAtClose = false;
            }
            break;

        case APIPalMsg_HidePalette_End:
            if (paletteOpenAtClose && HasInstance () && !GetInstance ().IsVisible ())
                GetInstance ().Show ();
            break;

        case APIPalMsg_DisableItems_Begin:
            if (HasInstance () && GetInstance ().IsVisible ())
                GetInstance ().DisableItems ();
            break;

        case APIPalMsg_DisableItems_End:
            if (HasInstance () && GetInstance ().IsVisible ())
                GetInstance ().EnableItems ();
            break;

        case APIPalMsg_IsPaletteVisible:
            *(reinterpret_cast<bool*> (param)) = HasInstance () && GetInstance ().IsVisible ();
            break;

        default:
            break;
    }

    return NoError;
}

GSErrCode NanoBananaPalette::RegisterPaletteControlCallBack ()
{
    return ACAPI_RegisterModelessWindow (
        NanoBananaPalette::PaletteRefId (),
        PaletteControlCallBack,
        API_PalEnabled_FloorPlan + API_PalEnabled_Section + API_PalEnabled_Elevation +
        API_PalEnabled_InteriorElevation + API_PalEnabled_3D + API_PalEnabled_Detail +
        API_PalEnabled_Worksheet + API_PalEnabled_Layout + API_PalEnabled_DocumentFrom3D,
        GSGuid2APIGuid (NanoBananaPalette::PaletteGuid ()));
}

// ---------------------------------------------------------------------------
// Menu item check state
// ---------------------------------------------------------------------------
void NanoBananaPalette::SetMenuItemCheckedState (bool isChecked)
{
    API_MenuItemRef itemRef   = {};
    GSFlags         itemFlags = {};

    itemRef.menuResID = ID_MENU_NANOBANANA;
    itemRef.itemIndex = NanoBananaPaletteMenuItemIndex;

    ACAPI_MenuItem_GetMenuItemFlags (&itemRef, &itemFlags);
    if (isChecked)
        itemFlags |= API_MenuItemChecked;
    else
        itemFlags &= ~API_MenuItemChecked;

    ACAPI_MenuItem_SetMenuItemFlags (&itemRef, &itemFlags);
}
