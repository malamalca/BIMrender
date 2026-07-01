# Changelog

All notable changes to BIMrender are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

### Added
- Prompt enhancement: a "✨ Enhance" button expands a brief instruction into a
  detailed photorealistic prompt via a Gemini text model
  (`gemini-2.5-flash`), independent of the active render provider
  (`EnhancePromptText` in [GeminiClient.cpp](Src/NanoBanana/GeminiClient.cpp),
  exposed to the panel as `ARUTILS.EnhancePrompt`).
- Prompt templates panel ("🎛 Templates"): compose a prompt fragment from
  style / time-of-day / weather / camera presets and insert it into the
  textarea.
- Persistent prompt history ("🕘 History"): the last 20 sent prompts are
  saved to `localStorage` and can be reused or cleared.
- Provider badge in the topbar showing the active AI provider (Gemini /
  Flux / Local), refreshed after settings changes.
- New shared `NanoBanana::JsonUtils` module
  ([JsonUtils.cpp](Src/NanoBanana/JsonUtils.cpp) /
  [JsonUtils.hpp](Src/NanoBanana/JsonUtils.hpp)) consolidating the
  `JsonEscape`, `DataUrlPayload`, and `JsonGetString` helpers that were
  previously duplicated across `GeminiClient.cpp`, `FluxClient.cpp`, and
  `WebUIClient.cpp`.
- `gemini-3.1-flash-lite-image` added to the Gemini model list and made the
  new default model.

### Changed
- `GeminiClient.cpp` request/response handling refactored around a shared
  `GeminiPostJson` helper, reused by both image rendering and the new
  prompt-enhancement call.
- Local WebUI (SD WebUI / Forge) rendering: lowered `steps` from 30 to 4 and
  added an explicit `cfg_scale` of 1.0 with `Euler` / `Beta` scheduler
  settings tuned for fast distilled models; output width/height are now
  rounded up to multiples of 16 to avoid `EinopsError` on FLUX/SDXL patch
  embeddings; inpaint denoising strength reduced from 0.75 to 0.6.
- `CheckEnvironment` in [BIMrender.c](Src/BIMrender.c) now falls back to
  literal add-on name/description strings if the resource strings fail to
  load, instead of leaving them empty.
- `SaveSettings` explicitly null-terminates every settings buffer after
  copying, rather than relying on a prior `memset`.

### Fixed
- None.

## [1.0.0] - 2026-06-25

### Added
- Initial public release of BIMrender, an Archicad add-on for AI-assisted
  rendering of 3D views ("NanoBanana" panel) with support for Gemini, Flux
  (Black Forest Labs), and a local Stable Diffusion WebUI/Forge backend.
- Prebuilt releases for Archicad 27 and Archicad 28.

[Unreleased]: https://github.com/malamalca/BIMrender/compare/d4ce141...HEAD
[1.0.0]: https://github.com/malamalca/BIMrender/commits/d4ce141
