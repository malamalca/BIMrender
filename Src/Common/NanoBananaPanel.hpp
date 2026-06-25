#pragma once

#ifndef NANOBANANA_PANEL_HPP
#define NANOBANANA_PANEL_HPP

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "DGModule.hpp"
#include "DGBrowser.hpp"

// ---------------------------------------------------------------------------
// NanoBananaPanel
//
// Non-panel base class that owns all shared controls, data and logic for both
// the modal dialog (NanoBananaDialog) and the modeless palette
// (NanoBananaPalette).  The constructor receives the concrete DG::Dialog& (the
// common base of DG::ModalDialog and DG::Palette) so it can initialise all
// child controls without itself being a Panel.
//
// All visible UI (capture button, image compare slider, prompt textarea, send
// button) lives in an embedded HTML page; this class bridges the page to the
// Archicad API:
//   - Capture3D()   captures the current 3D view to a PNG data URL
//   - Is3DActive()  reports whether the 3D window is active
//   - Render()      sends image + prompt to the Nano Banana model
//   - HasApiKey() / OpenSettings()  manage the stored API key
// ---------------------------------------------------------------------------
class NanoBananaPanel : public DG::PanelObserver
{
public:
    enum {
        BrowserId = 1
    };

protected:
    // DG::Dialog is the common base of DG::ModalDialog and DG::Palette.
    DG::Dialog& panel_;

    DG::Browser browser;

    explicit NanoBananaPanel (DG::Dialog& panel, short resId);
    ~NanoBananaPanel ();

    void InitBrowserControl ();
    void RegisterJavaScriptObject ();

    // The window's title-bar close box closes the panel: a modal dialog ends,
    // and the palette hides (see NanoBananaPalette::PanelCloseRequested).
    virtual void PanelOpened (const DG::PanelOpenEvent& ev) override;
    virtual void PanelResized (const DG::PanelResizeEvent& ev) override;
};

#endif // NANOBANANA_PANEL_HPP
