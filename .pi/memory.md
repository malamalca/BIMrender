# BIMrender — Project Memory

## Overview

**BIMrender** is an Archicad add-on (`.apx`) that captures the current 3D view and edits it with AI image models. The UI is a modeless palette (release) or modal dialog (debug) hosting an embedded HTML page in a `DG::Browser` control, bridged to C++ via a JavaScript object named `ARUTILS`.

- **License:** MIT (c) 2026 malamalca
- **Author:** Miha Nahtigal / ARHIM d.o.o.
- **Target Archicad versions:** 27, 28, 29 (Windows + macOS)
- **Primary preset used:** `ac29-INT` (API DevKit 29.3100), also `ac27-INT`, `ac28-INT`

## Directory Structure

```
BIMrender/
├── CMakeLists.txt              # Main build config — cross-platform, auto-detects AC version from DevKit
├── CMakePresets.json           # Presets for AC 27/28/29 on Windows + Mac
├── build.bat                   # Quick-build script (hardcoded paths to VS/CMake + DevKits)
├── .gitignore
├── LICENSE                     # MIT
├── README.md
├── Docs/
│   ├── ideas.md                # Feature backlog with effort estimates
│   ├── Local-A1111-Setup.md    # Guide for self-hosted SD WebUI
│   └── NanoBanana-Upload-Processing.md  # Detailed pipeline documentation
├── Src/
│   ├── APIEnvir.h              # Platform detection (WINDOWS/macintosh), ACExtension define
│   ├── ResourceIds.hpp         # All resource ID constants
│   ├── Common/
│   │   ├── NanoBananaPanel.cpp/cpp    # SHARED: browser host + JS bridge (ARUTILS object)
│   │   └── NanoBananaPanel.hpp
│   ├── Dialogs/
│   │   ├── dlgNanoBanana.cpp/.hpp      # Modal dialog wrapper (DEBUG builds only)
│   │   ├── dlgNanoBananaSettings.cpp/.hpp  # Settings modal dialog (provider/key/model)
│   └── NanoBanana/
│       ├── Base64.cpp/.hpp               # RFC 4648 encode/decode
│       ├── Capture3D.cpp/.hpp            # 3D view → temp PNG → base64 data URL
│       ├── FluxClient.cpp/.hpp           # BFL FLUX.1 Kontext + Fill (async submit→poll→download)
│       ├── GeminiClient.cpp/.hpp         # Google generateContent API (synchronous)
│       ├── Settings.cpp/.hpp             # Preferences persistence (versioned blob v1→v5)
│       └── WebUIClient.cpp/.hpp          # Local SD WebUI img2img/inpaint (synchronous)
├── Palettes/
│   ├── palNanoBanana.cpp/.hpp    # Modeless palette wrapper (RELEASE builds only) + singleton
├── RINT/                           # Language-specific resources (INT = international)
│   ├── BIMrender.grc              # Add-on name, menu strings
│   ├── dlgNanoBanana.grc          # Modal dialog layout (480×656, single Browser control)
│   ├── dlgNanoBananaSettings.grc  # Settings dialog (radios + text fields, 440×455)
│   └── palNanoBanana.grc          # Palette layout (480×656, single Browser control)
├── RFIX/                           # Platform-independent resources
│   ├── BIMrenderFix.grc           # MDID identifier + DATA resource pointing to nanoBanana.html
│   └── nanoBanana.html            # Embedded HTML page (~34KB) — ALL visible UI lives here
├── RFIX.win/
│   └── AddOnMain.rc2              # Windows-specific: icon + GRC includes
├── RFIX.mac/
│   └── Info.plist                 # macOS bundle info
├── Releases/                      # Built .apx files (not in git)
│   ├── BIMrender.apx              # AC29 build
│   ├── BIMrender27.apx
│   └── BIMrender28.apx
└── out/build/                     # CMake build output (gitignored)
```

## Architecture

### Three AI Providers

| Provider | Backend | Endpoint | Auth | Region Edit | Attachments | Pattern |
|----------|---------|----------|------|-------------|-------------|---------|
| **Gemini** | Google Generative Language API | `generateContent` | `x-goog-api-key` header | Magenta outline marker (soft) | ✅ Multi-image, up to 5 refs | Synchronous single request |
| **Flux** | Black Forest Labs FLUX.1 | Kontext + Fill endpoints | `x-key` header (optional for self-hosted) | Pixel-exact mask via Fill endpoint | ❌ Text-driven only | Async: submit → poll (1.5s, 120s max) → download |
| **Local** | SD WebUI (A1111/Forge) | `/sdapi/v1/img2img` | HTTP Basic auth (optional, inline URL) | Pixel-exact inpaint mask | ❌ Text-driven only | Synchronous single call |

### Data Flow

1. **Capture**: `ACAPI_ProjectOperation_Save` → temp PNG at 3D window pixel size → base64 data URL
2. **Render args** (fixed-position array sent from JS to C++):
   - `[0]` prompt (text)
   - `[1]` working image (PNG data URL)
   - `[2]` original capture (PNG data URL, only when iteration > 0)
   - `[3]` prompt history ("|" joined)
   - `[4]` region mask (PNG data URL, only for Local/Flux region edits)
   - `[5+]` reference attachments (JPEG data URLs q0.9, Gemini only)
3. **Result**: PNG data URL returned to JS → displayed in compare slider

### JS ↔ C++ Bridge (`ARUTILS`)

| Method | Return | Purpose |
|--------|--------|---------|
| `Is3DActive()` | bool | Is 3D window active? |
| `Capture3D()` | string (data URL or "") | Capture current 3D view |
| `HasApiKey()` | bool | Is active provider configured? |
| `OpenSettings()` | bool | Open settings dialog, return if now configured |
| `Render(args[])` | string (data URL or "ERROR: ...") | Send to active provider |
| `GetProvider()` | string ("gemini"/"flux"/"local") | Active backend name |
| `SaveImage(dataUrl)` | bool | Native save dialog |

### UI (nanoBanana.html)

Single-page embedded HTML with:
- **Top bar**: Capture button, Mark region button, Settings ⚙
- **Image stage**: Before/after compare slider (drag handle), overlay spinner
- **Attachments section**: Thumbnail strip with index badges, +Add button (max 5, JPEG q0.9)
- **Prompt textarea**: Free-text instructions
- **Bottom bar**: Status text, Undo ↶, Start over, Save 💾, Send (primary)

Key features in the HTML:
- Compare slider with `clipPath` on before image
- Region marquee: polygon drawn on `<canvas>` overlay, stored in image-native pixel coords
- History stack (max 10 entries, history[0] = original capture never dropped)
- Ctrl+Enter to send

### Settings Persistence

Versioned binary blob via `ACAPI_SetPreferences`/`ACAPI_GetPreferences`:
- **v5** (current): geminiKey[1024] + geminiModel[256] + provider[32] + fluxKey[1024] + fluxUrl[256] + fluxModel[256] + localUrl[256]
- Backwards-compatible migration from v1→v4 built in
- **WARNING**: Settings are saved INTO the project (`.pln`) file — API keys travel with saved projects

### Build System

- **CMake** auto-detects Archicad version from `ACAPinc.h` in DevKit
- Compiler: C++14 (AC<27), C++17 (AC27-28), C++20 (AC≥29)
- Toolset: v142 (AC≤28), v143 (AC≥29) on Windows
- Resources compiled via `CompileResources.py` from DevKit tools
- Output: `BIMrender.apx` (Windows shared lib) or `.appex` bundle (macOS)

### Key Constants

| Constant | Value | Location |
|----------|-------|----------|
| Default Gemini model | `gemini-3.1-flash-image` | Settings.cpp |
| Default Flux URL | `https://api.bfl.ai` | Settings.cpp |
| Default Flux model | `flux-kontext-pro` | Settings.cpp |
| Default Local URL | `http://127.0.0.1:7860` | Settings.cpp |
| WebUI denoising | 0.6 (whole), 0.6 (inpaint) | WebUIClient.cpp |
| WebUI steps | 4 | WebUIClient.cpp |
| WebUI cfg_scale | 1.0 | WebUIClient.cpp |
| Flux poll interval | 1500ms, max 120s | FluxClient.cpp |
| Gemini timeout | 120000ms | GeminiClient.cpp |
| Local timeout | 300000ms | WebUIClient.cpp |
| Max attachments | 5 | nanoBanana.html |
| Max history | 10 | nanoBanana.html |
| Palette GUID | `F1CF73DE-6510-4EB8-8893-5E30E333C6F0` | palNanoBanana.cpp |

### Resource IDs (ResourceIds.hpp)

| ID | Value | Purpose |
|----|-------|---------|
| `ID_ADDON_IDENTIFIER` | 32500 | MDID add-on identifier |
| `ID_ADDON_INFO` | 32000 | Add-on name/description string table |
| `ID_MENU_NANOBANANA` | 32510 | Menu strings (menu item "AI Render…") |
| `ID_MENU_PROMPT_NANOBANANA` | 32511 | Prompt strings |
| `ID_PALETTE_NANOBANANA` | 32600 | Modeless palette dialog resource |
| `ID_DLG_NANOBANANA_SETTINGS` | 32601 | Settings dialog resource |
| `ID_DLG_NANOBANANA` | 32602 | Modal dialog resource (debug) |
| `ID_DATA_NANOBANANA_HTML` | 32610 | DATA resource for embedded HTML |

## Feature Backlog (from Docs/ideas.md)

### Suggested sequencing:
1. **#2 Presets + #4 Retry/Cancel** — cheap, immediately smoother
2. **#1 Variations picker** — bigger UX win
3. **#3 Brush mask / multi-region / remove object** — real capability jump
4. Then #5 seed control, #6 upscale, #7 material library

### Priorities:
- **S (small)**: Architectural style preset buttons, auto-retry + cancel, seed control
- **M (medium)**: Variations picker, brush mask + multi-region, upscale
- **L (large)**: Material/reference library + "send result back to Archicad"

### Known constraints:
- Attachments are Gemini-only; Flux Fill and Local inpaint are text-driven
- Region edits: Gemini = soft marker; Flux Fill + Local inpaint = pixel-exact mask
- Gemini output is ~1 MP PNG (model-fixed)
- Denoising/steps are hardcoded in WebUIClient.cpp
- Settings persist into `.pln` files (API keys travel with projects)

## Build Commands

```bash
# Quick build (Windows, AC29 default):
build.bat

# Via CMake presets:
cmake --preset ac29-INT
cmake --build --preset ac29-INT   # Release
cmake --build --preset ac29-debug  # Debug

# For other versions:
cmake --preset ac27-INT && cmake --build --preset ac27-INT-Release
cmake --preset ac28-INT && cmake --build --preset ac28-INT
```

Requires `AC_API_DEVKIT_DIR` environment variable or preset cache variable pointing to the API Development Kit.

## Dependencies

- **Archicad API DevKit** (linked via `AC_API_DEVKIT_DIR`)
  - ACAP_STAT.lib / libACAP_STAT.a (static core)
  - All module headers and libs from DevKit
- **Windows**: MSVC v142 or v143 (VS 2019/2022 BuildTools)
- **macOS**: Xcode, Cocoa framework
- No external third-party libraries — all HTTP/JSON handled via Archicad SDK modules
