#pragma once

#ifndef DLG_NANOBANANA_HPP
#define DLG_NANOBANANA_HPP

#include "DG.h"
#include "DGModule.hpp"
#include "Common/NanoBananaPanel.hpp"

// ---------------------------------------------------------------------------
// NanoBananaDialog
//
// Modal dialog wrapper.  All controls and shared logic live in
// NanoBananaPanel.  This class only contributes the DG::ModalDialog identity.
// It is closed via the window's title-bar close box.  Being modal, the dialog
// is destroyed when it closes, so the add-on need not stay resident in memory.
// Used in DEBUG builds.
// ---------------------------------------------------------------------------
class NanoBananaDialog final : public DG::ModalDialog, public NanoBananaPanel
{
public:
    NanoBananaDialog ();
    ~NanoBananaDialog ();
};

// Opens the dialog modally.
void ShowNanoBananaDialog ();

#endif // DLG_NANOBANANA_HPP
