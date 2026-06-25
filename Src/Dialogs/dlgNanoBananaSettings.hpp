#pragma once

#ifndef DLG_NANOBANANA_SETTINGS_HPP
#define DLG_NANOBANANA_SETTINGS_HPP

#include "DG.h"
#include "DGModule.hpp"

// ---------------------------------------------------------------------------
// Small modal dialog to configure the "Nano Banana" image backend.
//
// A radio group switches between three providers:
//   - Google Gemini : an API key plus a model chosen from a dropdown.
//   - Flux          : a Black Forest Labs key, base URL and model name.
//   - Local         : a self-hosted SD WebUI (Forge / A1111) base URL.
//
// Everything is persisted via NanoBanana::SaveSettings on accept.
// ---------------------------------------------------------------------------
class NanoBananaSettingsDialog : public DG::ModalDialog,
                                 public DG::PanelObserver,
                                 public DG::ButtonItemObserver,
                                 public DG::RadioItemObserver
{
public:
    // NOTE: these are POSITIONAL ids — they must match the item order in
    // RINT/dlgNanoBananaSettings.grc.  The three radio buttons are kept on
    // consecutive ids (8,9,10) so DG treats them as one mutually-exclusive group.
    enum Controls {
        edtApiKeyId     = 1,
        btnOkId         = 2,
        btnCancelId     = 3,
        lblInfoId       = 4,
        edtModelId      = 5,
        lblApiKeyId     = 6,
        lblModelId      = 7,
        rbGeminiId      = 8,
        rbFluxId        = 9,
        rbLocalId       = 10,
        lblFluxKeyId    = 11,
        edtFluxKeyId    = 12,
        lblFluxUrlId    = 13,
        edtFluxUrlId    = 14,
        lblFluxModelId  = 15,
        edtFluxModelId  = 16,
        lblLocalUrlId   = 17,
        edtLocalUrlId   = 18
    };

    NanoBananaSettingsDialog ();
    ~NanoBananaSettingsDialog ();

private:
    DG::RadioButton rbGemini;
    DG::RadioButton rbFlux;
    DG::RadioButton rbLocal;

    DG::LeftText    lblApiKey;
    DG::TextEdit    edtApiKey;
    DG::LeftText    lblModel;
    DG::PopUp       popModel;

    DG::LeftText    lblFluxKey;
    DG::TextEdit    edtFluxKey;
    DG::LeftText    lblFluxUrl;
    DG::TextEdit    edtFluxUrl;
    DG::LeftText    lblFluxModel;
    DG::TextEdit    edtFluxModel;

    DG::LeftText    lblLocalUrl;
    DG::TextEdit    edtLocalUrl;

    DG::Button      btnOk;
    DG::Button      btnCancel;

    // Enables the controls of the selected provider, disables the other's.
    void UpdateEnabledState ();

    virtual void PanelOpened (const DG::PanelOpenEvent& ev) override;
    virtual void ButtonClicked (const DG::ButtonClickEvent& ev) override;
    virtual void RadioItemChanged (const DG::RadioItemChangeEvent& ev) override;
};

// Convenience: opens the dialog (returns true if the user saved).
bool ShowNanoBananaSettingsDialog ();

#endif // DLG_NANOBANANA_SETTINGS_HPP
