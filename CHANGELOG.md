# Changelog

All notable changes to BIMrender are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

### Fixed
- macOS: boolean return values from the `ARUTILS` JS bridge arrived in the
  page as empty strings (the CEF bridge on Mac does not marshal
  `JS::Value(bool)`), so the "Capture Current 3D" button never enabled, Send
  always opened the settings dialog first, and Save always reported
  "cancelled". The bridge now returns the strings `"true"`/`"false"` for all
  boolean results ([NanoBananaPanel.cpp](Src/Common/NanoBananaPanel.cpp)) and
  the page parses them with an `asBool()` helper that also accepts real
  booleans ([nanoBanana.html](RFIX/nanoBanana.html)), keeping Windows
  behaviour unchanged.
- Capturing failed with `APIERR_REFUSEDCMD` and saving the rendered image
  (or opening the settings dialog) could crash Archicad: neither the picture
  export nor a modal dialog may run inside a browser JS bridge callback (a
  modal dialog there nests an event loop inside a blocked CEF IPC call).
  These operations now run as a deferred module command
  (`NanoBanana::DeferredCommandHandler`, registered in
  [BIMrender.c](Src/BIMrender.c)) posted to the main event loop via
  `ACAPI_AddOnAddOnCommunication_CallFromEventLoop`; the page starts them
  with `Capture3D` / `SaveImage` / `OpenSettings` and polls `GetAsyncResult`
  until the result arrives (capture gives up with a clear error after 15
  seconds; the dialogs have no timeout)
  ([Capture3D.cpp](Src/NanoBanana/Capture3D.cpp),
  [NanoBananaPanel.cpp](Src/Common/NanoBananaPanel.cpp),
  [nanoBanana.html](RFIX/nanoBanana.html)). Note: the Demo version of
  Archicad refuses every save operation with the same error — the panel now
  reports this case explicitly ("The Demo version cannot save; a full license
  is required") instead of the generic "make sure the 3D window is active"
  message.
- Error messages in the panel status line were overwritten by the 1-second
  availability poll after a moment ("flashing"); they now stay visible until
  the next user action.

### Changed
- Misleading "not in 3D" guidance in the panel: the placeholder and status
  text said to *re-open the command* from the 3D window, but the palette
  polls the active window every second, so simply switching to the 3D window
  enables the Capture button. The texts in
  [nanoBanana.html](RFIX/nanoBanana.html) now say to switch to the 3D window.
- JS bridge hardening in [NanoBananaPanel.cpp](Src/Common/NanoBananaPanel.cpp):
  all `ARUTILS` handlers marshal their Archicad-touching work to the main
  thread via `GS::MessageLoopExecutor` (which thread CEF delivers JS callbacks
  on is platform-dependent: main thread on macOS, worker thread on Windows;
  ACAPI/DG calls are main-thread-only). Long-running network calls stay off
  the main thread so the UI keeps responding during a render. As part of
  this, `RenderImage` in [GeminiClient.cpp](Src/NanoBanana/GeminiClient.cpp)
  takes the model id as a parameter instead of reading Archicad preferences
  internally, and `InitBrowserControl` registers the JS bridge before loading
  the page.

## [1.0.1] - 2026-07-01

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

[Unreleased]: https://github.com/malamalca/BIMrender/compare/v1.0.1...HEAD
[1.0.1]: https://github.com/malamalca/BIMrender/compare/d4ce141...v1.0.1
[1.0.0]: https://github.com/malamalca/BIMrender/commits/d4ce141
