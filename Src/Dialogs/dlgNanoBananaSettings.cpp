#include "APIEnvir.h"
#include "ACAPinc.h"
#include <ResourceIds.hpp>

#include "Dialogs/dlgNanoBananaSettings.hpp"
#include "NanoBanana/Settings.hpp"

NanoBananaSettingsDialog::NanoBananaSettingsDialog () :
    DG::ModalDialog (ACAPI_GetOwnResModule (), ID_DLG_NANOBANANA_SETTINGS, ACAPI_GetOwnResModule ()),
    rbGemini     (GetReference (), rbGeminiId),
    rbFlux       (GetReference (), rbFluxId),
    rbLocal      (GetReference (), rbLocalId),
    lblApiKey    (GetReference (), lblApiKeyId),
    edtApiKey    (GetReference (), edtApiKeyId),
    lblModel     (GetReference (), lblModelId),
    popModel     (GetReference (), edtModelId),
    lblFluxKey   (GetReference (), lblFluxKeyId),
    edtFluxKey   (GetReference (), edtFluxKeyId),
    lblFluxUrl   (GetReference (), lblFluxUrlId),
    edtFluxUrl   (GetReference (), edtFluxUrlId),
    lblFluxModel (GetReference (), lblFluxModelId),
    edtFluxModel (GetReference (), edtFluxModelId),
    lblLocalUrl  (GetReference (), lblLocalUrlId),
    edtLocalUrl  (GetReference (), edtLocalUrlId),
    btnOk        (GetReference (), btnOkId),
    btnCancel    (GetReference (), btnCancelId)
{
    Attach (*this);
    btnOk.Attach (*this);
    btnCancel.Attach (*this);
    rbGemini.Attach (*this);
    rbFlux.Attach (*this);
    rbLocal.Attach (*this);
}

NanoBananaSettingsDialog::~NanoBananaSettingsDialog ()
{
    rbGemini.Detach (*this);
    rbFlux.Detach (*this);
    rbLocal.Detach (*this);
    btnOk.Detach (*this);
    btnCancel.Detach (*this);
    Detach (*this);
}

void NanoBananaSettingsDialog::UpdateEnabledState ()
{
    const bool gemini = rbGemini.IsSelected ();
    const bool flux   = rbFlux.IsSelected ();
    const bool local  = rbLocal.IsSelected ();

    lblApiKey.SetStatus (gemini);
    edtApiKey.SetStatus (gemini);
    lblModel.SetStatus  (gemini);
    popModel.SetStatus  (gemini);

    lblFluxKey.SetStatus   (flux);
    edtFluxKey.SetStatus   (flux);
    lblFluxUrl.SetStatus   (flux);
    edtFluxUrl.SetStatus   (flux);
    lblFluxModel.SetStatus (flux);
    edtFluxModel.SetStatus (flux);

    lblLocalUrl.SetStatus (local);
    edtLocalUrl.SetStatus (local);
}

void NanoBananaSettingsDialog::PanelOpened (const DG::PanelOpenEvent&)
{
    // Gemini fields.
    edtApiKey.SetText (NanoBanana::LoadApiKey ());

    // Select the stored model in the dropdown; fall back to the first entry
    // (the default) if the stored value isn't one of the offered options.
    const GS::UniString stored = NanoBanana::LoadModel ();
    const short itemCount = popModel.GetItemCount ();
    short toSelect = 1;
    for (short i = 1; i <= itemCount; ++i) {
        if (popModel.GetItemText (i) == stored) {
            toSelect = i;
            break;
        }
    }
    if (itemCount > 0)
        popModel.SelectItem (toSelect);

    // Flux fields.
    edtFluxKey.SetText (NanoBanana::LoadFluxKey ());
    edtFluxUrl.SetText (NanoBanana::LoadFluxUrl ());
    edtFluxModel.SetText (NanoBanana::LoadFluxModel ());

    // Local (SD WebUI / Forge) fields.
    edtLocalUrl.SetText (NanoBanana::LoadLocalUrl ());

    // Provider radio.
    switch (NanoBanana::LoadProvider ()) {
        case NanoBanana::Provider::Flux:  rbFlux.Select ();   break;
        case NanoBanana::Provider::Local: rbLocal.Select ();  break;
        default:                          rbGemini.Select (); break;
    }

    UpdateEnabledState ();
}

void NanoBananaSettingsDialog::RadioItemChanged (const DG::RadioItemChangeEvent&)
{
    UpdateEnabledState ();
}

void NanoBananaSettingsDialog::ButtonClicked (const DG::ButtonClickEvent& ev)
{
    if (ev.GetSource () == &btnCancel) {
        PostCloseRequest (DG::ModalDialog::Cancel);
    } else if (ev.GetSource () == &btnOk) {
        NanoBanana::Provider provider = NanoBanana::Provider::Gemini;
        if (rbFlux.IsSelected ())       provider = NanoBanana::Provider::Flux;
        else if (rbLocal.IsSelected ()) provider = NanoBanana::Provider::Local;

        const short sel = popModel.GetSelectedItem ();
        const GS::UniString model = (sel >= 1) ? popModel.GetItemText (sel)
                                               : NanoBanana::DefaultModel ();

        NanoBanana::SaveSettings (provider,
                                  edtApiKey.GetText (),
                                  model,
                                  edtFluxKey.GetText (),
                                  edtFluxUrl.GetText (),
                                  edtFluxModel.GetText (),
                                  edtLocalUrl.GetText ());
        PostCloseRequest (DG::ModalDialog::Accept);
    }
}

bool ShowNanoBananaSettingsDialog ()
{
    NanoBananaSettingsDialog dialog;
    return dialog.Invoke ();
}
