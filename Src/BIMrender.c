// *****************************************************************************
// BIMrender — AI rendering add-on for Archicad (NanoBanana).
//
// Captures the current 3D view and edits it with an image model (Google Gemini,
// Black Forest Labs Flux, or a self-hosted SD WebUI / Forge).  All the feature
// code lives under Src/NanoBanana, Src/Common, Src/Dialogs and Src/Palettes.
// *****************************************************************************

#ifdef DEBUG
#define POGO_NOHANDLERS
#endif

#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h
#include	"ACAPI_Goodies.h"
#include	"DG.h"
#include	"ResourceIds.hpp"

// NanoBanana surfaces as a modal dialog in DEBUG builds and as a modeless
// palette in release builds; both share NanoBananaPanel (Common/).
#ifdef DEBUG
#include	"Dialogs/dlgNanoBanana.hpp"
#else
#include	"Palettes/palNanoBanana.hpp"
#endif

// -----------------------------------------------------------------------------
// Open / toggle the NanoBanana AI render UI.
//   DEBUG   -> modal dialog (destroyed on close)
//   release -> modeless palette (toggle show/hide, kept in memory)
// -----------------------------------------------------------------------------
static void ShowNanoBanana ()
{
#ifdef DEBUG
	ShowNanoBananaDialog ();
#else
	if (NanoBananaPalette::HasInstance () && NanoBananaPalette::GetInstance ().IsVisible ()) {
		NanoBananaPalette::GetInstance ().Hide ();
	} else {
		if (!NanoBananaPalette::HasInstance ())
			NanoBananaPalette::CreateInstance ();
		NanoBananaPalette::GetInstance ().Show ();
	}
#endif
}

// -----------------------------------------------------------------------------
// Handles menu commands
// -----------------------------------------------------------------------------
GSErrCode MenuCommandHandler (const API_MenuParams *menuParams)
{
	switch (menuParams->menuItemRef.menuResID) {
		case ID_MENU_NANOBANANA:
			switch (menuParams->menuItemRef.itemIndex) {
				case NanoBananaPaletteMenuItemIndex:
					ShowNanoBanana ();
					break;
			}
			break;
	}

	return NoError;
} // MenuCommandHandler


// =============================================================================
//
// Required functions
//
// =============================================================================

// -----------------------------------------------------------------------------
// Dependency definitions
// -----------------------------------------------------------------------------
API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, ID_ADDON_INFO, 1, ACAPI_GetOwnResModule());
	RSGetIndString (&envir->addOnInfo.description, ID_ADDON_INFO, 2, ACAPI_GetOwnResModule());

	return APIAddon_Normal;
} // CheckEnvironment


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------
GSErrCode RegisterInterface (void)
{
	GSErrCode err;

	err = ACAPI_MenuItem_RegisterMenu (ID_MENU_NANOBANANA, ID_MENU_PROMPT_NANOBANANA, MenuCode_UserDef, MenuFlag_Default);
	if (DBERROR (err != NoError))
		return err;

	return NoError;
} // RegisterInterface


// -----------------------------------------------------------------------------
// Called when the Add-On has been loaded into memory to perform an operation
// -----------------------------------------------------------------------------
GSErrCode Initialize (void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (ID_MENU_NANOBANANA, MenuCommandHandler);
	if (DBERROR (err != NoError))
		return err;

	// In release builds NanoBanana is a modeless palette; register its callback.
#ifndef DEBUG
	err = NanoBananaPalette::RegisterPaletteControlCallBack ();
	if (DBERROR (err != NoError))
		return err;
#endif

	return NoError;
} // Initialize


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------
GSErrCode FreeData (void)
{
#ifndef DEBUG
	if (NanoBananaPalette::HasInstance ())
		NanoBananaPalette::DestroyInstance ();
#endif
	return NoError;
} // FreeData
