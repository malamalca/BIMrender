#include "APIEnvir.h"
#include "ACAPinc.h"

#include "NanoBanana/Settings.hpp"

#include <cstring>

namespace NanoBanana {

// Versioned preferences blob.
//   v1: apiKey only (1024 bytes)
//   v2: apiKey + model
//   v3: apiKey + model + provider + serverUrl + localModel   (transitional)
//   v4: geminiKey + geminiModel + provider + fluxKey + fluxUrl + fluxModel
//   v5: v4 + localUrl   (self-hosted SD WebUI / Forge)
static const Int32  kPrefsVersion = 5;

static const char*  kDefaultModel     = "gemini-3.1-flash-image";
static const char*  kDefaultFluxUrl   = "https://api.bfl.ai";
static const char*  kDefaultFluxModel = "flux-kontext-pro";
static const char*  kDefaultLocalUrl  = "http://127.0.0.1:7860";

static const char*  kProviderGemini = "gemini";
static const char*  kProviderFlux   = "flux";
static const char*  kProviderLocal  = "local";

struct Prefs {
    char geminiKey[1024];
    char geminiModel[256];
    char provider[32];      // "gemini" | "flux" | "local"
    char fluxKey[1024];
    char fluxUrl[256];
    char fluxModel[256];
    char localUrl[256];
};

GS::UniString DefaultModel ()     { return GS::UniString (kDefaultModel,     CC_UTF8); }
GS::UniString DefaultFluxUrl ()   { return GS::UniString (kDefaultFluxUrl,   CC_UTF8); }
GS::UniString DefaultFluxModel () { return GS::UniString (kDefaultFluxModel, CC_UTF8); }
GS::UniString DefaultLocalUrl ()  { return GS::UniString (kDefaultLocalUrl,  CC_UTF8); }

// Reads the stored preferences into a fully-initialised Prefs (with defaults),
// transparently upgrading the older v1..v3 layouts.
static void GetPrefs (Prefs& p)
{
    std::memset (&p, 0, sizeof (p));
    std::strncpy (p.geminiModel, kDefaultModel,     sizeof (p.geminiModel) - 1);
    std::strncpy (p.provider,    kProviderGemini,   sizeof (p.provider) - 1);
    std::strncpy (p.fluxUrl,     kDefaultFluxUrl,   sizeof (p.fluxUrl) - 1);
    std::strncpy (p.fluxModel,   kDefaultFluxModel, sizeof (p.fluxModel) - 1);
    std::strncpy (p.localUrl,    kDefaultLocalUrl,  sizeof (p.localUrl) - 1);

    Int32  version = 0;
    GSSize nByte   = 0;
    if (ACAPI_GetPreferences (&version, &nByte, nullptr) != NoError)
        return;

    if (version == 5 && nByte == static_cast<GSSize> (sizeof (Prefs))) {
        ACAPI_GetPreferences (&version, &nByte, &p);
        p.geminiKey[sizeof (p.geminiKey) - 1]     = '\0';
        p.geminiModel[sizeof (p.geminiModel) - 1] = '\0';
        p.provider[sizeof (p.provider) - 1]       = '\0';
        p.fluxKey[sizeof (p.fluxKey) - 1]         = '\0';
        p.fluxUrl[sizeof (p.fluxUrl) - 1]         = '\0';
        p.fluxModel[sizeof (p.fluxModel) - 1]     = '\0';
        p.localUrl[sizeof (p.localUrl) - 1]       = '\0';
        if (p.geminiModel[0] == '\0') std::strncpy (p.geminiModel, kDefaultModel,     sizeof (p.geminiModel) - 1);
        if (p.provider[0]    == '\0') std::strncpy (p.provider,    kProviderGemini,   sizeof (p.provider) - 1);
        if (p.fluxUrl[0]     == '\0') std::strncpy (p.fluxUrl,     kDefaultFluxUrl,   sizeof (p.fluxUrl) - 1);
        if (p.fluxModel[0]   == '\0') std::strncpy (p.fluxModel,   kDefaultFluxModel, sizeof (p.fluxModel) - 1);
        if (p.localUrl[0]    == '\0') std::strncpy (p.localUrl,    kDefaultLocalUrl,  sizeof (p.localUrl) - 1);
        return;
    }

    if (version == 4) {
        // v4 layout: same as v5 minus localUrl.
        struct PrefsV4 { char geminiKey[1024]; char geminiModel[256]; char provider[32];
                         char fluxKey[1024]; char fluxUrl[256]; char fluxModel[256]; } v4;
        if (nByte == static_cast<GSSize> (sizeof (PrefsV4))) {
            std::memset (&v4, 0, sizeof (v4));
            Int32  v = 0;
            GSSize n = static_cast<GSSize> (sizeof (PrefsV4));
            ACAPI_GetPreferences (&v, &n, &v4);
            std::memcpy (p.geminiKey,   v4.geminiKey,   sizeof (p.geminiKey));
            std::memcpy (p.geminiModel, v4.geminiModel, sizeof (p.geminiModel));
            std::memcpy (p.provider,    v4.provider,    sizeof (p.provider));
            std::memcpy (p.fluxKey,     v4.fluxKey,     sizeof (p.fluxKey));
            std::memcpy (p.fluxUrl,     v4.fluxUrl,     sizeof (p.fluxUrl));
            std::memcpy (p.fluxModel,   v4.fluxModel,   sizeof (p.fluxModel));
            p.geminiKey[sizeof (p.geminiKey) - 1]     = '\0';
            p.geminiModel[sizeof (p.geminiModel) - 1] = '\0';
            p.provider[sizeof (p.provider) - 1]       = '\0';
            p.fluxKey[sizeof (p.fluxKey) - 1]         = '\0';
            p.fluxUrl[sizeof (p.fluxUrl) - 1]         = '\0';
            p.fluxModel[sizeof (p.fluxModel) - 1]     = '\0';
            if (p.geminiModel[0] == '\0') std::strncpy (p.geminiModel, kDefaultModel,     sizeof (p.geminiModel) - 1);
            if (p.provider[0]    == '\0') std::strncpy (p.provider,    kProviderGemini,   sizeof (p.provider) - 1);
            if (p.fluxUrl[0]     == '\0') std::strncpy (p.fluxUrl,     kDefaultFluxUrl,   sizeof (p.fluxUrl) - 1);
            if (p.fluxModel[0]   == '\0') std::strncpy (p.fluxModel,   kDefaultFluxModel, sizeof (p.fluxModel) - 1);
            // localUrl keeps its default
        }
        return;
    }

    if (version == 3) {
        // v3 (transitional): apiKey + model + provider + serverUrl + localModel.
        struct PrefsV3 { char apiKey[1024]; char model[256]; char provider[32]; char serverUrl[256]; char localModel[256]; } v3;
        if (nByte == static_cast<GSSize> (sizeof (PrefsV3))) {
            std::memset (&v3, 0, sizeof (v3));
            Int32  v = 0;
            GSSize n = static_cast<GSSize> (sizeof (PrefsV3));
            ACAPI_GetPreferences (&v, &n, &v3);
            v3.apiKey[sizeof (v3.apiKey) - 1]         = '\0';
            v3.model[sizeof (v3.model) - 1]           = '\0';
            v3.provider[sizeof (v3.provider) - 1]     = '\0';
            v3.serverUrl[sizeof (v3.serverUrl) - 1]   = '\0';
            v3.localModel[sizeof (v3.localModel) - 1] = '\0';
            std::strncpy (p.geminiKey, v3.apiKey, sizeof (p.geminiKey) - 1);
            if (v3.model[0] != '\0')
                std::strncpy (p.geminiModel, v3.model, sizeof (p.geminiModel) - 1);
            if (std::strcmp (v3.provider, "local") == 0)
                std::strncpy (p.provider, kProviderFlux, sizeof (p.provider) - 1);
            if (v3.serverUrl[0] != '\0')
                std::strncpy (p.fluxUrl, v3.serverUrl, sizeof (p.fluxUrl) - 1);
            if (v3.localModel[0] != '\0')
                std::strncpy (p.fluxModel, v3.localModel, sizeof (p.fluxModel) - 1);
        }
        return;
    }

    if (version == 2) {
        // v2 layout: apiKey[1024] + model[256].
        struct PrefsV2 { char apiKey[1024]; char model[256]; } v2;
        if (nByte == static_cast<GSSize> (sizeof (PrefsV2))) {
            std::memset (&v2, 0, sizeof (v2));
            Int32  v = 0;
            GSSize n = static_cast<GSSize> (sizeof (PrefsV2));
            ACAPI_GetPreferences (&v, &n, &v2);
            v2.apiKey[sizeof (v2.apiKey) - 1] = '\0';
            v2.model[sizeof (v2.model) - 1]   = '\0';
            std::strncpy (p.geminiKey, v2.apiKey, sizeof (p.geminiKey) - 1);
            if (v2.model[0] != '\0')
                std::strncpy (p.geminiModel, v2.model, sizeof (p.geminiModel) - 1);
        }
        return;
    }

    if (version == 1 && nByte == 1024) {
        // Legacy key-only blob: read it into geminiKey, keep default model.
        Int32  v = 0;
        GSSize n = 1024;
        ACAPI_GetPreferences (&v, &n, p.geminiKey);
        p.geminiKey[sizeof (p.geminiKey) - 1] = '\0';
    }
}

static void PutPrefs (const Prefs& p)
{
    ACAPI_SetPreferences (kPrefsVersion, sizeof (Prefs), &p);
}

GS::UniString LoadApiKey ()
{
    Prefs p;
    GetPrefs (p);
    return GS::UniString (p.geminiKey, CC_UTF8);
}

GS::UniString LoadModel ()
{
    Prefs p;
    GetPrefs (p);
    GS::UniString model (p.geminiModel, CC_UTF8);
    model.Trim ();
    return model.IsEmpty () ? DefaultModel () : model;
}

GS::UniString LoadFluxKey ()
{
    Prefs p;
    GetPrefs (p);
    return GS::UniString (p.fluxKey, CC_UTF8);
}

GS::UniString LoadFluxUrl ()
{
    Prefs p;
    GetPrefs (p);
    GS::UniString url (p.fluxUrl, CC_UTF8);
    url.Trim ();
    return url.IsEmpty () ? DefaultFluxUrl () : url;
}

GS::UniString LoadFluxModel ()
{
    Prefs p;
    GetPrefs (p);
    GS::UniString model (p.fluxModel, CC_UTF8);
    model.Trim ();
    return model.IsEmpty () ? DefaultFluxModel () : model;
}

GS::UniString LoadLocalUrl ()
{
    Prefs p;
    GetPrefs (p);
    GS::UniString url (p.localUrl, CC_UTF8);
    url.Trim ();
    return url.IsEmpty () ? DefaultLocalUrl () : url;
}

Provider LoadProvider ()
{
    Prefs p;
    GetPrefs (p);
    if (std::strcmp (p.provider, kProviderFlux) == 0)  return Provider::Flux;
    if (std::strcmp (p.provider, kProviderLocal) == 0) return Provider::Local;
    return Provider::Gemini;
}

bool IsConfigured ()
{
    Prefs p;
    GetPrefs (p);
    if (std::strcmp (p.provider, kProviderLocal) == 0) {
        // A local server always has a usable default URL (127.0.0.1:7860).
        return true;
    }
    if (std::strcmp (p.provider, kProviderFlux) == 0) {
        // A key is enough (cloud or authenticated self-host); otherwise a
        // non-default server URL means a keyless self-hosted endpoint.
        if (p.fluxKey[0] != '\0')
            return true;
        return p.fluxUrl[0] != '\0' && std::strcmp (p.fluxUrl, kDefaultFluxUrl) != 0;
    }
    return p.geminiKey[0] != '\0';
}

void SaveSettings (Provider provider,
                   const GS::UniString& geminiKey,
                   const GS::UniString& geminiModel,
                   const GS::UniString& fluxKey,
                   const GS::UniString& fluxUrl,
                   const GS::UniString& fluxModel,
                   const GS::UniString& localUrl)
{
    Prefs p;
    GetPrefs (p);   // start from existing so nothing is clobbered

    GS::UniString gKey  = geminiKey;   gKey.Trim ();
    GS::UniString gMdl  = geminiModel; gMdl.Trim ();
    GS::UniString fKey  = fluxKey;     fKey.Trim ();
    GS::UniString fUrl  = fluxUrl;     fUrl.Trim ();
    GS::UniString fMdl  = fluxModel;   fMdl.Trim ();
    GS::UniString lUrl  = localUrl;    lUrl.Trim ();
    if (gMdl.IsEmpty ()) gMdl = DefaultModel ();
    if (fUrl.IsEmpty ()) fUrl = DefaultFluxUrl ();
    if (fMdl.IsEmpty ()) fMdl = DefaultFluxModel ();
    if (lUrl.IsEmpty ()) lUrl = DefaultLocalUrl ();

    const char* prov = (provider == Provider::Flux)  ? kProviderFlux  :
                       (provider == Provider::Local) ? kProviderLocal : kProviderGemini;

    std::memset (p.geminiKey,   0, sizeof (p.geminiKey));
    std::memset (p.geminiModel, 0, sizeof (p.geminiModel));
    std::memset (p.provider,    0, sizeof (p.provider));
    std::memset (p.fluxKey,     0, sizeof (p.fluxKey));
    std::memset (p.fluxUrl,     0, sizeof (p.fluxUrl));
    std::memset (p.fluxModel,   0, sizeof (p.fluxModel));
    std::memset (p.localUrl,    0, sizeof (p.localUrl));

    std::strncpy (p.geminiKey,   gKey.ToCStr (0, MaxUSize, CC_UTF8).Get (), sizeof (p.geminiKey) - 1);
    std::strncpy (p.geminiModel, gMdl.ToCStr (0, MaxUSize, CC_UTF8).Get (), sizeof (p.geminiModel) - 1);
    std::strncpy (p.provider,    prov,                                      sizeof (p.provider) - 1);
    std::strncpy (p.fluxKey,     fKey.ToCStr (0, MaxUSize, CC_UTF8).Get (), sizeof (p.fluxKey) - 1);
    std::strncpy (p.fluxUrl,     fUrl.ToCStr (0, MaxUSize, CC_UTF8).Get (), sizeof (p.fluxUrl) - 1);
    std::strncpy (p.fluxModel,   fMdl.ToCStr (0, MaxUSize, CC_UTF8).Get (), sizeof (p.fluxModel) - 1);
    std::strncpy (p.localUrl,    lUrl.ToCStr (0, MaxUSize, CC_UTF8).Get (), sizeof (p.localUrl) - 1);

    PutPrefs (p);
}

} // namespace NanoBanana
