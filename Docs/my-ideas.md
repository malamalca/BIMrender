# BIMrender — Additional Ideas & Code Review Notes

Generated from full codebase analysis (2026-06-28). Complements the existing
[ideas.md](./ideas.md) backlog.  Effort: **S** = small, **M** = medium, **L** = large.

> **Verified 2026-06-28** against the AC 27 devkit and the post-refactor source.
> Corrections from that pass are folded in below (notably A1 is largely done, the
> A2 threading reality, the D3 token claim, and several invented API names in
> sections C/G).

---

## A. Code Quality & Refactoring (found while reading source)

These are not user-facing features but reduce technical debt before adding new
ones.

### A1. Extract shared helpers — *S, ~done*

**Mostly complete.** `JsonEscape()` and `DataUrlPayload()` already live in
`JsonUtils.cpp/.hpp`, and the `JStr()` / `JGetStr()` duplication is gone —
`GeminiClient.cpp`, `FluxClient.cpp` and `WebUIClient.cpp` all now call the
unified `JsonGetString()`.

The **only** remaining duplication is `SplitDataUrl()` in `GeminiClient.cpp`,
which is a superset of `DataUrlPayload()` (it also returns the mime type). Fold
it into `JsonUtils` with an optional `mime` out-param and delete the local copy.
Scope is ~10 lines, not the ~60 originally estimated.

### A2. Render progress callback — *M, high UX payoff*

The `ARUTILS.Render()` bridge call **blocks the UI thread** for the entire
request (Gemini ~5-15s, Flux poll up to 120s, Local CPU minutes). The current
workaround is a double `requestAnimationFrame` so the spinner actually paints
before the call starts — but after that the UI freezes.

Add an optional progress callback to the bridge:

```cpp
// JS side: ARUTILS.Render(args, onProgress)
// C++ side: poll callback invoked every N seconds via DG timer or a separate
//           thread, reporting "polling attempt 3/48" or "encoding..." etc.
```

For Flux this is natural — the poll loop already has an iteration counter.
For Gemini and Local a simple "still working…" heartbeat suffices. This also
enables **Cancel** (#4 in ideas.md) because the C++ side can check a flag each
iteration.

> **Reality check — re-tag as L, not M.** `ARUTILS.Render()` runs *synchronously
> on the UI thread* (see the threading note in
> [NanoBanana-Upload-Processing.md](./NanoBanana-Upload-Processing.md)). While
> the thread is blocked inside `FluxRenderImage`, it **cannot** paint a progress
> update or observe a user-set cancel flag — both require moving the request
> *off* the UI thread and marshalling results back, which couples this to **G4
> (background processing, L)**. A same-thread "progress callback" alone changes
> nothing visible.
>
> **Resolve first:** does `RegisterAsynchJSObject` deliver the native callback on
> a worker thread or the UI thread? The "Asynch" name hints at a worker, yet the
> docs report a UI freeze. If it is already off-thread, the freeze has another
> cause and A2 may shrink back toward M; if it is on the UI thread, A2 is a
> redesign. Split into **A2a:** heartbeat status text (S, cosmetic) and **A2b:**
> true off-thread render + cancel (L).

### A3. Hot-reload HTML during development — *S, DX*

The page is compiled into the `.apx` as a `DATA` resource, meaning every CSS /
JS change requires a full rebuild + reload in Archicad. In `NanoBananaPanel.cpp`,
add a `#ifdef DEBUG` path that loads from an external file instead:

```cpp
#ifdef DEBUG
    // Try external file first; fall back to embedded resource on failure.
    IO::Location htmlFile (IO::GetSpecialFolder (APISP_UserPluginSettings),
                           IO::Name ("nanoBanana.dev.html"));
    if (fileExists (htmlFile)) { resourceData = readFile (htmlFile); }
    else { resourceData = loadFromResource (); }
#else
    resourceData = loadFromResource ();
#endif
```

Place `nanoBanana.dev.html` next to the `.apx` in the Archicad Add-Ons folder.
Reload the palette and pick up changes instantly.

### A4. API key security warning — *S, important*

Settings are persisted via `ACAPI_SetPreferences` which saves **into the
project (.pln) file**. This means every saved project contains raw Gemini / Flux
API keys. Add:

1. A warning label in the settings dialog: *"Keys are stored in the project file. Do not share .pln files containing API keys."*
2. Optionally, a separate "global" storage path (e.g., user AppData folder) for
   keys, with the project-level blob only storing provider choice + model name.

---

## B. UX Improvements (beyond existing backlog)

### B1. Negative prompt support (Local) — *S*

The SD WebUI `img2img` API accepts a `negative_prompt` field. Currently nothing
is sent, so the model uses its default (usually empty). For architectural viz,
a good negative prompt dramatically improves results:

```
"wireframe, flat color, cartoon, illustration, blurry, lowres, deformed,
  extra limbs, poorly drawn, text, watermark, signature"
```

Add a hidden "Advanced" section in settings (or a toggle in the HTML) with a
negative prompt textarea for the Local provider. Gemini and Flux don't use this
field so it's cleanly isolated to `WebUIClient.cpp`.

### B2. Adjustable denoising strength — *S, Local*

`kDenoisingStrength = 0.6` is hardcoded in `WebUIClient.cpp`. Some models need
lower values (0.3-0.4 for subtle material changes) while others benefit from
higher (0.7+ for dramatic style transfers). Add a slider in the HTML page or
settings:

- **Subtle** (0.3) — change materials, keep everything else
- **Balanced** (0.6) — current default, good all-rounder
- **Strong** (0.8) — major lighting/style overhaul, geometry may drift

Store in settings or pass through the render args array (slot 6).

### B3. Batch save / "save all history" — *S*

Currently only the latest render can be saved. When iterating through multiple
edits, users often want to compare final candidates side by side outside
Archicad. Add a "Save All…" option that writes `ai_render_01.png` through
`ai_render_N.png` into a single folder. Uses the existing `history[]` array in
the HTML page and calls `ARUTILS.SaveImage()` for each entry (or add a new batch
bridge method).

### B4. Image info tooltip — *S*

Show the working image's resolution and file size on hover over the stage area.
Captures at 4K window size produce huge base64 strings that slow down every
subsequent edit (the original capture is re-sent each iteration for Gemini).
This helps users understand why a render is slow and encourages them to resize
the 3D window before capturing.

### B5. Keyboard shortcut for Send — *S, already partially done*

Ctrl+Enter is already bound in the HTML. Add **Space** as an alternative when
focus is not in the textarea (i.e., a global "press Space to render" when the
palette has focus but the prompt doesn't). This matches the mental model of
"stage is set → press go".

### B6. Provider badge in UI — *S*

Show which provider is active somewhere visible (e.g., small label next to the
⚙ settings button: "Gemini" / "Flux" / "Local"). Right now a user who switches
providers in settings has no visual confirmation when they return to the palette.

---

## C. Archicad Integration (deeper than current backlog)

> **API-name caveat.** Several function names originally cited in sections C and
> G do not exist verbatim in the AC 27 devkit — they were plausible guesses.
> Verified replacements are noted inline below. Treat any remaining unmarked name
> as indicative until checked against `Support/Inc`.

### C1. Capture saved 3D views instead of current — *M*

Instead of only capturing whatever is currently displayed, let the user pick from
Archicad's saved 3D view list.

> **Correction:** `ACAPI_3DDrawing_Get` / `ACAPI_Document_SetActiveWindow` are not
> real. Saved-view enumeration goes through the **Navigator** APIs
> (`ACAPI_Navigator_*` / `ACAPI_AddOnIntegration_RegisterNavigatorAddOnViewPointDataHandler`);
> the current window's sight is `ACAPI_3D_GetCurrentWindowSight`. This is more
> involved than "set active window + capture" — likely **M+**.

Use case: "Render my hero perspective at dusk" without manually navigating to it.

This could be a dropdown next to the Capture button or a separate "Batch render
all 3D views" mode.

### C2. Auto-capture resolution setting — *S*

Currently the capture resolution equals the 3D window's on-screen pixel size
(`API_SavePars_Picture` has no width/height fields). Add a settings option:

- **Window size** (current behavior)
- **Fixed resolution** (e.g., 1920×1080, 2048×2048) — requires capturing to a
  larger off-screen buffer if the window is small

For the fixed option, Archicad's `ACAPI_ProjectOperation_Save` may not support
this directly, but the Local backend could upscale afterward via the WebUI's
`/sdapi/v1/scripts` (ResizeAndCrop) or the image could be resized before sending.

### C3. Read project location for sun angle — *M*

Archicad stores project geolocation and north orientation. Reading these allows
auto-generating a realistic sun position prompt: *"Golden hour, sun from
southwest (165°), latitude 46°N (Ljubljana)"*. This makes exterior renders
dramatically more realistic without any user effort.

> **Correction — and good news:** `ACAPI_ProjectSettings_GetProjectLocation` is not
> real, but the capability is *stronger* than assumed. Use
> `ACAPI_GeoLocation_GetGeoLocation` for the place/north data and, even better,
> `ACAPI_GeoLocation_CalcSunOnPlace` / `ACAPI_GeoLocation_GetSunSets` to have
> Archicad compute the actual sun vector for a given date/time — feed that
> straight into the prompt. Bumps this idea's value up.

### C4. Insert render into Layout/Worksheet — *M+/L (re-scoped)*

Instead of only saving to disk, add an "Insert to Layout" button that:

1. Decodes the result image to a temp file
2. Places it on the active layout or worksheet
3. Optionally links it so it updates on re-render

> **Correction:** there is no one-call `ACAPI_Document_InsertPicture`. You create
> a Drawing or Figure **element** (`ACAPI_Element_Create` with the appropriate
> element type / drawing link), which is materially more work than a button —
> re-scope from M toward **M+/L** and prototype the element-creation path first.

This closes the loop for clients who want AI renders in presentation sets.

### C5. Store render settings per 3D view — *M*

When a user perfects a render for a specific saved 3D view (prompt, attachments,
provider), store those settings keyed to the view. Reopening that view auto-fills
the prompt and attachments. This turns BIMrender into a per-view rendering preset
system.

> **Correction:** `ACAPI_CustomObject` is not a real function. Options for
> per-view storage are the navigator viewpoint-data handlers
> (`ACAPI_AddOnIntegration_RegisterNavigatorAddOnViewPointDataHandler`) or
> add-on-owned data in the project; both need design work, so this stays **M+**.

---

## D. Provider-Specific Enhancements

### D1. Flux: expose `prompt_strength` — *S*

The FLUX.1 Kontext API supports a `prompt_strength` parameter (0.0–1.0, default
~0.8) controlling how much the output deviates from the input image. Lower =
stay closer to original geometry; higher = more creative freedom. This is
architecturally critical — too high and columns move, too low and nothing
changes.

Add this as a slider in settings or render args. No code architecture change
needed — it's one more field in the submit JSON.

### D2. Flux: seed support — *S*

The BFL API accepts a `seed` parameter. If the user gets a composition they like,
showing and allowing them to lock the seed means prompt iteration doesn't
reshuffle the entire image. The seed appears in the response and can be round-
tripped.

### D3. Gemini: system instruction — *S (cleanliness + quality experiment)*

Today everything goes into a single `user` turn: the prompt, the dynamic notes
(original-capture hint, attachment numbering), the inlined history context, and
then the static `kDefaultBaseRules` block appended **last** — deliberately, so
the model weights it most (recency). D3 lifts `kDefaultBaseRules` into a
top-level `systemInstruction` (sibling of `contents`).

**Impact — reframed after review:**

- **Cleaner separation (the real win):** immutable quality rules leave the
  per-request content; the user turn becomes just the instruction + dynamic
  notes. Easier to tune and reason about.
- **Adherence: *maybe* better — treat as an A/B test, not a guaranteed win.**
  System instructions are stickier for Gemini's text path, but the current code
  already exploits recency weighting, and image-generation models can weight
  instruction placement differently than text models. Measure it, and keep the
  ability to revert.
- **Not a token saving.** `systemInstruction` tokens are counted and billed on
  every request just like user-turn tokens — the bytes go over the wire either
  way. The original "saves context-window tokens" rationale is incorrect; real
  per-call savings would need *context caching* (a separate feature).
- **Effort: genuinely S, fully isolated to `GeminiClient.cpp`** (one structural
  change to the body JSON; drop `fullPrompt += kDefaultBaseRules`). Flux/Local
  untouched. Caveat: confirm the configured image model accepts
  `systemInstruction` on `v1beta:generateContent`.

### D4. Local: model list from WebUI — *S*

Instead of using whatever checkpoint is loaded, query `/sdapi/v1/sd-models` on
connect and show a dropdown in settings. Switching models via
`/sdapi/v1/options` (`sd_model_checkpoint` field) lets the user pick from
installed checkpoints without touching the WebUI separately.

### D5. Local: Hires. fix / second pass — *M*

The WebUI supports `enable_hr: true` with separate `hr_scale`, `hr_second_pass_steps`,
and `hr_upscaler` fields in the img2img API. This renders at low res then
upscales and refines — producing sharper output than a single pass at 1024px.

Add a checkbox "High-res fix" in settings that enables this for the Local
backend. Complements #6 (upscale) from ideas.md but does it in one API call.

---

## E. Prompt Engineering (architectural domain)

### E1. Smart prompt templates — *M*

Instead of free-text only, provide a template system with placeholders:

```
Photorealistic architectural photography of {style} building,
{time_of_day} lighting, {weather}, shot on {camera},
{additional_notes}. Keep all geometry exactly as in the input.
```

The HTML page offers dropdowns for each placeholder:
- **Style**: Modernist / Brutalist / Mediterranean / Scandinavian / Industrial / ...
- **Time**: Golden hour / Blue hour / Midday overcast / Night interior / ...
- **Weather**: Clear / Light fog / Light snow / Rain wet surfaces / ...
- **Camera**: Wide angle 16mm / Standard 35mm / Telephoto 85mm / Drone aerial / ...

Each choice maps to proven prompt fragments. Users can still override with free
text. This directly addresses the "I don't know what to type" problem and
produces more consistent results than raw prompts.

### E2. Prompt history panel — *S*

Show previously used prompts in a small scrollable list (persisted in settings
or localStorage). Clicking one re-fills the textarea. Architectural viz users
develop a handful of "go-to" prompts they reuse across projects.

### E3. "Enhance prompt" via Gemini text — *S*

Add a small ✨ button next to the Send button that sends the user's brief prompt
to Gemini (text-only, no image) with instructions to expand it into a detailed
architectural photography prompt. The expanded version replaces the original in
the textarea. This costs a tiny text API call but dramatically improves results
for users who write "make it look nice".

---

## F. Performance & Reliability

### F1. Thumbnail cache for history — *S*

Each history entry stores a full-resolution PNG data URL. At 4K capture size,
10 entries = ~200MB of base64 strings in JS memory. Generate small thumbnails
(256px max dimension) for the compare slider and display, keeping only the last
entry at full resolution. The "Save" button reconstructs from the stored full-res
version.

### F2. Compress capture before sending — *S*

The 3D capture is sent as lossless PNG base64. For Gemini (which re-encodes
internally) and Flux, JPEG at quality 0.85 would reduce payload by 60-80% with
no visible quality loss in the output. The original-capture reference for Gemini
could stay PNG for detail recovery, but the working image could be JPEG.

### F3. Connection test on settings save — *S*

When the user clicks OK in settings, make a lightweight probe request:
- **Gemini**: call `models/{model}` (GET, no image) to verify the key works
- **Flux**: same — or submit a tiny task
- **Local**: call `/sdapi/v1/options` to verify the server is reachable

Show "✓ Connected" or "✗ Cannot reach server" in the status line. Catches wrong
URLs and expired keys before the user wastes time on a full render.

### F4. Request timeout configurability — *S*

Current timeouts: Gemini 120s, Flux poll 120s, Local 300s. For slow local CPU
renders, 300s may not be enough. Make the Local timeout configurable in settings.

---

## G. Stretch / Moonshot Ideas

### G1. Multi-view consistency — *L*

Render the same architectural style across multiple 3D views (exterior front,
exterior side, interior) with consistent materials and lighting. Requires:
- Capture multiple views
- Use a locked seed + shared prompt template
- Possibly a "style reference" image from the first render fed into subsequent
  renders as an attachment

### G2. Real-time preview (Local only) — *L*

For the Local backend with a fast GPU, show progressive previews during
generation. The WebUI API doesn't support streaming, but a websocket extension
or polling `/sdapi/v1/internal/cmd-queue` could give progress bars at least.

### G3. Archicad material → attachment pipeline — *L*

When the user marks a region on a wall, read the wall's surface from Archicad,
and if it has an image-based texture, automatically add that texture as an
attachment reference. This creates a powerful "change this wall's material"
workflow without manual file picking.

> **Correction:** `ACAPI_Element_GetSurface` is not real, and the access path is
> multi-step (keeps this firmly **L**):
> 1. `ACAPI_Element_Get` on the element, then read the surface index from its
>    type-specific fields — there is no generic accessor (e.g. a wall has
>    `refMat` / `oppMat` / `sidMat` / `topMat` / `botMat`, other element types
>    differ), so the element→surface mapping must be handled per element type.
> 2. `ACAPI_Attribute_Get` for that surface (the API calls a surface a
>    **material**, `API_MaterialID`) → `API_MaterialType`, whose `.texture`
>    (`API_Texture`) carries `textureName` + a `fileLoc` to the image file.
> 3. Load that file and add it as an attachment.
>
> Note: `ACAPI_Element_GetSurfaceQuantities` is a false friend — it returns
> areas/quantities, not the surface attribute.

### G4. Render queue with background processing — *L*

Move rendering to a background thread (or a separate helper process) so the
palette stays responsive during long renders. Results arrive via callback.
Enables batch rendering multiple views or variations simultaneously.

---

## Suggested Priority Order (my recommendations)

Based on code review, here's what I'd tackle first:

| Phase | Items | Why |
|-------|-------|-----|
| **Now** | A4 (key warning label), A1 finish (fold in `SplitDataUrl`), A3 (hot-reload HTML), F3 (connection test) | A4 is a real data-exfiltration risk (keys saved in `.pln`) — ship the warning now; the rest is zero user-facing risk / immediate dev relief. A1 is a ~10-line leftover. |
| **Soon** | B6 (provider badge), E2 (prompt history), B4 (image info), A4 step 2 (split key storage out of project) | Small changes, address real user confusion + finish the key-security work |
| **Next** | A2a (heartbeat text, S), B1 (negative prompt), B2 (denoising slider), D3 (Gemini system instruction) | Clean per-provider isolation; D3 is a low-risk quality experiment |
| **Later** | E1 (prompt templates), D4 (model list), A2b (off-thread render + cancel, L) | Domain value / the real UX unfreeze; A2b couples to G4 |
| **Future** | C3 (sun angle — verified APIs, high payoff), C1 (saved-view capture, M+), C4 (insert to layout, M+/L), G3 (material pipeline) | Deep Archicad integration; verify API surface before committing estimates |
