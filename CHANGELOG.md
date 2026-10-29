# Changelog

All notable changes to BIMrender are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

### Added
- **Archicad 30 support**, alongside Archicad 27, 28 and 29 from the same
  `CMakeLists.txt`. No add-on source code needed changing: every Archicad API
  entry point, selector, struct field and GS/DG/IO symbol the add-on uses is
  declared identically in the Archicad 30 DevKit (the headers were reformatted
  — pointer placement, doc comments — but no signature, struct layout or
  enumerator the add-on touches changed). What did need work is the build:
  - **Version resource** ([RFIX.win/AddOnMain.rc2](RFIX.win/AddOnMain.rc2)).
    Archicad 30 reads the Archicad version an add-on was built for from the
    `PRODUCTVERSION` field of the Win32 version resource and rejects a mismatch
    with "the file is either not an add-on or an outdated add-on that cannot be
    used with this version of Archicad". BIMrender had no version resource at
    all, so one was added, following the Archicad 30 add-on template's layout
    (`PRODUCTVERSION 0,<Archicad version>,0,0`; the GS build number is 0 on
    Windows). `FILEVERSION` stays the add-on's own version. The whole block is
    conditional on `AC_API_VERSION`, which only the Archicad 30 and newer
    builds define, so the Archicad 27-29 resource output is unchanged
    byte for byte — those versions generated no version resource and never read
    one.
  - **Project-local resource compiler** ([Tools/](Tools/README.md)). The
    Archicad 30 DevKit no longer ships `CompileResources.py` in `Support/Tools`:
    it is part of the add-on project template now, and its command line changed.
    The project carries its own copy, with two documented deviations from the
    stock file — it hands `RFIX.win/AddOnMain.rc2` to `rc` unchanged instead of
    wrapping every generated `.rc2` into one include file (which would define
    every resource twice, since `AddOnMain.rc2` already includes them by name),
    and it passes `/d AC_API_VERSION=<version>` so the Archicad version reaches
    the version resource. `CMakeLists.txt` picks the DevKit's own script for
    Archicad 27-29 and this copy for Archicad 30 and newer, and fails with a
    clear message if the DevKit build number cannot be read from
    `VersionAndBuildNumber.txt` (the Archicad 30 script needs it to select the
    `ResConv` options that generate the Dark Mode icons).
  - **Build files**: `ac30-INT` and `debug30` configure presets plus `ac30-INT`
    and `ac30-debug` build presets ([CMakePresets.json](CMakePresets.json)), and
    an Archicad 30 pass in [build.bat](build.bat). Following the existing
    convention that the newest version is the unsuffixed build,
    `Releases/BIMrender.apx` is now the Archicad 30 build and the Archicad 29
    build moved to `Releases/BIMrender29.apx`. Note that from Archicad 30 on
    these files are no longer interchangeable — Archicad checks
    `PRODUCTVERSION` and refuses a build made for another version.

  Archicad 30 uses the same C++20 standard and MSVC v143 toolset as Archicad 29,
  so the compiler settings are unchanged. The Archicad 30 add-on folder is
  `%PROGRAMFILES%\GRAPHISOFT\Archicad 30\Add-Ons\`.
- Place the render into the project as a picture: a "🖼 Place" button in the
  panel creates a figure (picture element) from the current render in the
  active 2D window — floor plan, section/elevation, detail, worksheet, 3D
  document or layout — centred in the current zoom at 60% of the visible
  area, in a "Place AI Render" undo step
  ([PlacePicture.cpp](Src/NanoBanana/PlacePicture.cpp),
  `ARUTILS.PlaceImage` in
  [NanoBananaPanel.cpp](Src/Common/NanoBananaPanel.cpp)). The figure's aspect
  ratio comes from the PNG/JPEG header, read directly from the image bytes.
  Creating an element needs command context, so the placement runs as a
  deferred module-command task like the 3D capture. The button follows the
  active window through the panel's 1-second poll
  (`ARUTILS.CanPlaceImage`) and stays disabled in the 3D window, where a
  figure cannot be placed.
- **Open image…**: a render can start from an image file instead of a 3D
  capture — a photo, a sketch, an earlier render, a view exported from
  somewhere else ([SaveImageFile.cpp](Src/NanoBanana/SaveImageFile.cpp),
  `ARUTILS.LoadImage` in
  [NanoBananaPanel.cpp](Src/Common/NanoBananaPanel.cpp),
  [nanoBanana.html](RFIX/nanoBanana.html)). The opened image replaces the
  capture as the thing edits start from, through the same code path, so the
  compare slider, Mark region, Undo, Place and Save all behave as they do after
  a capture. The button needs no 3D window, so it is always live.

  On Windows it opens Archicad's own file dialog, in a deferred UI task like
  Save as…, filtering on PNG / JPEG / WebP and reopening in the folder the last
  image came from. The format is read from the file's **magic bytes** rather
  than its name, so a mislabelled file is refused instead of reaching the model
  as garbage, and files over 32 MB are refused before being read. Where the
  host cannot drive its own dialog (macOS, see the Save as… entry) the bridge
  answers `"NODIALOG"` and the page asks with its own `<input type="file">` —
  the picker the attachment list already uses, which works on both platforms,
  so **Open image… works on macOS today** even though Save as… does not.

  An opened image is scaled down to 2048 px on the long edge if it is larger:
  the models normalize their output to about 1 MP, so a 24 MP photo only costs
  upload time. A PNG stays PNG, keeping its alpha and its lossless source the
  way a capture does; anything else becomes JPEG q0.92 rather than ballooning a
  photo into a multi-megabyte PNG. An image already small enough and already
  PNG or JPEG is passed through untouched.
- **Save as…**: the render is saved where the user chooses again, through the
  host's native Save dialog, instead of being written to
  `<Documents>/BIMrender` without asking
  ([SaveImageFile.cpp](Src/NanoBanana/SaveImageFile.cpp), `ARUTILS.SaveImage`
  in [NanoBananaPanel.cpp](Src/Common/NanoBananaPanel.cpp)). A modal dialog may
  not be opened inside a browser JS-bridge callback, so `SaveImage` now only
  *starts* the save: it posts a deferred UI task — `StartDeferredUiTask`, the
  same message-loop dispatch the settings dialog uses — and the page polls
  `GetAsyncResult` with no timeout for `"SAVED:<path>"`, `"CANCELLED"` or
  `"ERROR: <reason>"`. The dialog offers only the file type the render actually
  is (PNG, JPEG or WebP, from the data-URL mime; the add-on registers its own
  PNG and WebP types, since the DevKit registers neither), appends that
  extension to a name typed without one, and reopens where the previous save
  went for the rest of the Archicad session.

  On macOS the Save dialog is still not used: `DG::FileDialog` there is a native
  `NSSavePanel` whose own modal run loop does not pump the JavaScript engine's
  message pump, so the page's poll is never serviced and Archicad hangs (run
  inline in the bridge callback it crashes outright, which is why the dialog was
  dropped for an auto-save in the first place).
  `NanoBanana::IsSaveDialogAvailable()` returns false there and the task
  auto-saves and reveals the file, exactly as every platform did before — so
  macOS behaviour is unchanged. The Mac fix is an asynchronous sheet
  (`[NSSavePanel beginSheetModalForWindow:completionHandler:]`), which returns to
  the event loop instead of nesting a run loop; `SaveImageFile.cpp` carries the
  notes, `CMakeLists.txt` now globs `*.mm` on non-Windows for the Objective-C++
  translation unit it needs, and because its completion handler runs after the
  deferred task has returned, the result will have to be pushed into the page
  with `DG::Browser::ExecuteJS` rather than picked up by the poll. Setting
  `BIMRENDER_SAVE_DIALOG=1` in the environment forces the DG dialog on macOS for
  testing, without a rebuild.

### Fixed
- The Archicad 30 build could not compile its resources at all: `ResConv`
  failed with "Can't open file: nanoBanana.html", so `BIMrenderFix.grc` and with
  it the whole build stopped. The Archicad 29 DevKit's `CompileResources.py`
  handed `ResConv` two resource search paths (`RFIX/Images;RFIX`), but the
  Archicad 30 add-on template — which the project's own copy in
  [Tools/](Tools/README.md) is taken from — passes only `RFIX/Images`. A file a
  `.grc` embeds by name from `RFIX` itself can then never be found, and because
  the script `chdir`s to its own folder a bare name does not resolve on its own
  either. The project copy passes both folders again, as a third documented
  local deviation from the stock file.
- Error messages and the "Choose where to save…" prompt were overwritten by the
  panel's 1-second availability poll, which also fired three bridge calls per
  tick into a main thread that an open modal dialog was blocking. The poll now
  pauses while a deferred task is in flight
  ([nanoBanana.html](RFIX/nanoBanana.html)).
- Choosing a PNG in the Open dialog was refused with "the selected file is not
  a file", and the dialog offered no image filter at all — only "All Files".
  Both came from how the add-on registered its file types
  ([SaveImageFile.cpp](Src/NanoBanana/SaveImageFile.cpp)), and the rules were
  established by exercising `FTM::FileTypeManager` directly rather than by
  inference:
  - **Two types declared the same `image/png` mime.** The manager keeps one
    hash-indexed mime table across every type, so the second registration
    fails outright and `AddType` returns `UnknownType`. Filtering on that is
    what made the dialog reject the file.
  - **The JPEG filter used the DevKit's own `FTM::JPEGFileType`.**
    `AddTypeToGroup` refuses a type another manager owns, so the group never
    contained `jpg`, the registration check failed, and the fallback dropped
    every filter — leaving "All Files".

  Each type is now registered by the add-on's own manager into its own group,
  with no mime strings, and the dialogs carry named filters: "Images (*.png,
  *.jpg, *.jpeg, *.webp)" first, then one per format. (`AddExtension` does
  split a `';'`-separated list, so one type covers both `jpg` and `jpeg`;
  a creator and MAC type of `0` turned out to be harmless after all.)

  A filter still cannot stop a file being chosen: the registration is verified
  with `IsExtensionInGroup`, and if it ever fails again both dialogs go up
  unfiltered rather than refusing files they should accept. A save takes its
  extension from `WithExtension`, and an opened file is identified by its magic
  bytes rather than by the filter.
- Opening a PNG or JPEG was still refused with "The selected file is not in
  Images", even with the registration above verified. The dialog checks the
  chosen file against the selected filter by identifying its type across every
  registered manager, and Archicad had already registered `png` / `jpg`, so the
  add-on's own group never matched. The Open dialog now carries no type filter;
  the file's magic bytes decide whether it is a PNG, JPEG or WebP
  ([SaveImageFile.cpp](Src/NanoBanana/SaveImageFile.cpp)). The Save dialog is
  unchanged.
- `NbDebugLog` wrote to `/tmp/BIMrender_debug.log`, which on Windows resolves
  against the current drive's root and almost never exists, so the Windows
  build logged nothing at all. It writes to `%TEMP%` there now
  ([Capture3D.cpp](Src/NanoBanana/Capture3D.cpp)), and a file that cannot be
  opened reports the `IO::File` error code with it, so a refusal can be told
  apart from a locked or missing file.
- The panel reported "Switch to the 3D window to capture a view" as an error
  once a second whenever the 3D window was not active, even with an image
  already loaded and nothing about to be captured — it buried the real status
  under a red message. It is only shown while there is nothing to work on
  ([nanoBanana.html](RFIX/nanoBanana.html)).
- The top bar could not be made narrower than its row of buttons: it was a
  single non-wrapping flex row, so in a narrow palette the settings button was
  pushed out of reach. It wraps now ([nanoBanana.html](RFIX/nanoBanana.html)).
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
- When a deferred operation cannot start, the panel now shows the host's
  actual reason (another operation still in progress, or the scheduling
  error code) instead of a generic message, and a task stuck in the pending
  state is abandoned after 30 seconds so it cannot block every subsequent
  capture/save/settings operation.
- Build: editing [nanoBanana.html](RFIX/nanoBanana.html) did not re-run the
  resource compilation, so the add-on kept the page compiled in by an earlier
  build while the C++ side was rebuilt. The page is embedded by name from
  `BIMrenderFix.grc` ('DATA' ID_DATA_NANOBANANA_HTML), and the resource step
  only depended on the `.grc` files themselves, which such an edit leaves
  untouched — a page-only change therefore never reached the binary, and a
  mixed build could pair a new JS bridge with an old page (after the bridge
  was renamed to `GetAsyncResult`, that combination broke the 3D capture).
  [CMakeLists.txt](CMakeLists.txt) now globs the embedded data files into
  `AddOnDataFiles` and lists them as dependencies of the resource step on both
  platforms.
- Archicad 29 on Windows never ran the deferred module command: the
  `ACAPI_AddOnAddOnCommunication_CallFromEventLoop` post returned `NoError`
  but `DeferredCommandHandler` was never called back (an add-on posting a
  command to itself), so every 3D capture pended until the page's 15-second
  timeout. `FetchDeferredResult` — already polled every 150 ms by the page —
  now doubles as the timer: when the handler has not been entered 1.5 seconds
  after the post, the task is run from the message loop instead
  ([Capture3D.cpp](Src/NanoBanana/Capture3D.cpp)), the same dispatch the
  settings dialog uses, and still outside the CEF callback stack the module
  command existed to escape. Whichever path arrives first wins; the other
  becomes a no-op.
- Prompt history rows collapsed to a few pixels high once the saved prompts
  exceeded the list's `max-height`, hiding the prompt text: the rows are flex
  children of `#histList` and shrank by default. They now keep their height
  and the list scrolls instead ([nanoBanana.html](RFIX/nanoBanana.html)).
- Template preset pickers could only be used by holding the mouse button down
  while dragging onto an option: the host's embedded browser opens a native
  `<select>` popup only while the button is held, and that popup is drawn
  outside the page, so it cannot be styled or controlled from JS. Each
  `<select>` is now backed by an in-page dropdown (one click opens, a second
  picks, a click anywhere else or Escape closes) that flips above the button
  and caps its height when the palette is short. The `<select>` elements stay
  in the DOM as the value store, so the `<option>` markup and
  `buildTemplate()` are unchanged ([nanoBanana.html](RFIX/nanoBanana.html)).

### Changed
- Prompt tool buttons reordered to "🎛 Templates · ✨ Enhance · 🕘 History"
  ([nanoBanana.html](RFIX/nanoBanana.html)).
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
