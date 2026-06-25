// *****************************************************************************
// NanoBanana AI render MODAL DIALOG
//
// Thin wrapper - all controls and shared logic live in NanoBananaPanel
// (Common/).  Used in DEBUG builds; release builds use NanoBananaPalette.
// *****************************************************************************

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "DG.h"
#include <ResourceIds.hpp>

#include "Dialogs/dlgNanoBanana.hpp"

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------
NanoBananaDialog::NanoBananaDialog () :
    DG::ModalDialog (ACAPI_GetOwnResModule (), ID_DLG_NANOBANANA, ACAPI_GetOwnResModule ()),
    NanoBananaPanel (*this, ID_DLG_NANOBANANA)
{
}

NanoBananaDialog::~NanoBananaDialog ()
{
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
void ShowNanoBananaDialog ()
{
    NanoBananaDialog dialog;

    if (dialog.GetId () == 0) {
        DGAlert (DG_ERROR, "NanoBanana",
            "The dialog resource could not be loaded.",
            GS::UniString (), "OK");
        return;
    }
    dialog.Invoke ();
}
