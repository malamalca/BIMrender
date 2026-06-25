#pragma once

#ifndef NANOBANANA_PALETTE_HPP
#define NANOBANANA_PALETTE_HPP

#include "DG.h"
#include "DGModule.hpp"
#include "Common/NanoBananaPanel.hpp"

// ---------------------------------------------------------------------------
// NanoBananaPalette
//
// Modeless palette wrapper.  All controls and shared logic live in
// NanoBananaPanel.  This class handles the palette lifecycle (singleton,
// show/hide, Archicad callback registration) and the palette-only
// PanelCloseRequested event.  Used in release builds.
// ---------------------------------------------------------------------------
class NanoBananaPalette final : public DG::Palette, public NanoBananaPanel
{
    static GS::Ref<NanoBananaPalette> instance;

    static GSErrCode PaletteControlCallBack (Int32 paletteId, API_PaletteMessageID messageID, GS::IntPtr param);

    void SetMenuItemCheckedState (bool isChecked);

    virtual void PanelCloseRequested (const DG::PanelCloseRequestEvent& ev, bool* accepted) override;

    NanoBananaPalette ();

public:
    ~NanoBananaPalette ();

    static bool                HasInstance ();
    static void                CreateInstance ();
    static NanoBananaPalette&  GetInstance ();
    static void                DestroyInstance ();

    void Show ();
    void Hide ();

    static GSErrCode        RegisterPaletteControlCallBack ();
    static Int32            PaletteRefId ();
    static const GS::Guid&  PaletteGuid ();
};

#endif // NANOBANANA_PALETTE_HPP
