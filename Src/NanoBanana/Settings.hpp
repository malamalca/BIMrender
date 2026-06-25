#pragma once

#ifndef NANOBANANA_SETTINGS_HPP
#define NANOBANANA_SETTINGS_HPP

#include "UniString.hpp"

namespace NanoBanana {

// Which backend produces the image.
enum class Provider {
    Gemini,     // Google Generative Language API (default)
    Flux,       // Black Forest Labs FLUX.1 Kontext (cloud or self-hosted)
    Local       // self-hosted SD WebUI (AUTOMATIC1111 / Forge) img2img
};

// Persisted in the Archicad preferences file via ACAPI_Set/GetPreferences.

// --- Gemini -----------------------------------------------------------------
GS::UniString LoadApiKey ();            // Gemini API key
GS::UniString LoadModel ();             // Gemini model; returns DefaultModel() when unset
GS::UniString DefaultModel ();          // the built-in default Gemini model id

// --- Flux -------------------------------------------------------------------
GS::UniString LoadFluxKey ();           // Black Forest Labs x-key (empty for keyless self-hosting)
GS::UniString LoadFluxUrl ();           // base URL; returns DefaultFluxUrl() when unset
GS::UniString LoadFluxModel ();         // Flux model id; returns DefaultFluxModel() when unset
GS::UniString DefaultFluxUrl ();        // "https://api.bfl.ai"
GS::UniString DefaultFluxModel ();      // "flux-kontext-pro"

// --- Local (SD WebUI / Forge) -----------------------------------------------
GS::UniString LoadLocalUrl ();          // base URL; returns DefaultLocalUrl() when unset
GS::UniString DefaultLocalUrl ();       // "http://127.0.0.1:7860"

// --- Common -----------------------------------------------------------------
Provider LoadProvider ();               // returns Provider::Gemini when unset
bool     IsConfigured ();               // true when the active provider has enough to run

// Saves all fields together (a single preferences blob is used).
void SaveSettings (Provider provider,
                   const GS::UniString& geminiKey,
                   const GS::UniString& geminiModel,
                   const GS::UniString& fluxKey,
                   const GS::UniString& fluxUrl,
                   const GS::UniString& fluxModel,
                   const GS::UniString& localUrl);

} // namespace NanoBanana

#endif // NANOBANANA_SETTINGS_HPP
