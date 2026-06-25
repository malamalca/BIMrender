# NanoBanana — Image Upload & Processing

How the NanoBanana AI-render feature captures an image, uploads it to an image
backend, and turns the response back into a picture in the UI.

This documents the **data flow and encoding**, not the UI chrome. For the
palette/dialog split see the source under `Src/Palettes`, `Src/Dialogs`, and
`Src/Common`.

---

## 1. Components

| Layer | File | Role |
|-------|------|------|
| UI page | [`RFIX/nanoBanana.html`](../RFIX/nanoBanana.html) | All visible UI; runs in a `DG::Browser`. Captures intent, builds the request args, shows the result. |
| Browser host + JS bridge | [`Src/Common/NanoBananaPanel.cpp`](../Src/Common/NanoBananaPanel.cpp) | Hosts the page and registers the `ARUTILS` JS object that bridges the page to the Archicad API. |
| 3D capture | [`Src/NanoBanana/Capture3D.cpp`](../Src/NanoBanana/Capture3D.cpp) | Saves the current 3D view to a temp PNG and returns it as a base64 data URL. |
| Gemini client | [`Src/NanoBanana/GeminiClient.cpp`](../Src/NanoBanana/GeminiClient.cpp) | Google Generative Language API (`generateContent`). Multi-image, single request. |
| Flux client | [`Src/NanoBanana/FluxClient.cpp`](../Src/NanoBanana/FluxClient.cpp) | Black Forest Labs FLUX.1 Kontext. Async submit → poll → download. |
| Local client | [`Src/NanoBanana/WebUIClient.cpp`](../Src/NanoBanana/WebUIClient.cpp) | Self-hosted SD WebUI (AUTOMATIC1111 / Forge) `img2img`. Single synchronous call. |
| Settings | [`Src/NanoBanana/Settings.cpp`](../Src/NanoBanana/Settings.cpp) | Provider choice, API keys, model, server URL. Persisted via `ACAPI_SetPreferences`. |
| Base64 | [`Src/NanoBanana/Base64.cpp`](../Src/NanoBanana/Base64.cpp) | RFC 4648 encode/decode for the data-URL ↔ raw-bytes conversions. |

---

## 2. The JS ↔ C++ bridge

The page never talks to the network or the API directly. It calls methods on a
JS object named `ARUTILS`, registered by `RegisterJavaScriptObject()` via
`browser.RegisterAsynchJSObject(...)`. Every method returns a **Promise**.

| `ARUTILS` method | Direction | Purpose |
|------------------|-----------|---------|
| `Is3DActive()` | page → AC | Is the 3D window the active one? |
| `Capture3D()` | page → AC | Capture the 3D view → PNG data URL. |
| `HasApiKey()` | page → AC | Is the active provider configured? (`IsConfigured()`) |
| `OpenSettings()` | page → AC | Open the settings dialog; returns whether configured afterward. |
| `Render(args)` | page → AC | Send the image(s) + prompt to the active provider; resolve with the result data URL or `"ERROR: …"`. |
| `GetProvider()` | page → AC | Active backend (`"gemini" \| "flux" \| "local"`); the page uses it to choose the region-edit payload. |
| `SaveImage(dataUrl)` | page → AC | Native save dialog for the last render. |

> **Threading note.** The `Render` bridge call ties up the UI thread while the
> request is in flight. The page therefore sets the "processing…" status and
> spinner, yields two animation frames so the browser actually paints that
> state, and only then invokes `ARUTILS.Render(...)`. See `doRender()`.

---

## 3. Capture: 3D view → image

`CaptureCurrent3DAsDataUrl()`:

1. Verifies the 3D model window is active.
2. `ACAPI_ProjectOperation_Save` writes the 3D view to a temp **PNG**
   (`APIFType_PNGFile`, 24-bit true color, no dithering, no crop).
3. Reads the bytes back, base64-encodes them, and returns
   `data:image/png;base64,…`.

**Dimensions are not set by the add-on** — `API_SavePars_Picture` has no
width/height fields, so Archicad saves at the **current 3D window's on-screen
pixel size**. The input resolution therefore varies with window/monitor size
and can exceed 1 MP.

---

## 4. What gets uploaded

The page builds a fixed-position argument array in `doRender()` (parsed by
`GetRenderParams` in `NanoBananaPanel.cpp`):

```
args = [ prompt, workingImage, originalCapture, promptHistory, mask, ...attachments ]
```

| Idx | Slot | Content | Format | Sent when |
|----|------|---------|--------|-----------|
| 0 | `prompt` | the user instruction | text | always |
| 1 | `workingImage` | current image being edited (the capture, then each edit's output) | PNG data URL | always |
| 2 | `originalCapture` | the first, full-quality 3D capture, used as a detail/quality reference | **PNG** data URL | only after the first edit (`iteration > 0`); `""` on the first edit, and `""` for region edits |
| 3 | `promptHistory` | previous prompts, `"\|"`-joined, inlined as context | text | when history exists |
| 4 | `mask` | region mask (white = edit area on black) | PNG data URL | only for a **Local** region edit; `""` otherwise |
| 5+ | `attachments` | user-uploaded reference images | **JPEG** data URLs (q0.9) | when present (Gemini only) |

### Attachment handling

- The file picker accepts `image/png, image/jpeg, image/webp`, multiple, up to
  `maxRefs` (5).
- On upload, each file is read with `FileReader`, then **re-encoded to JPEG at
  quality 0.9** via a `<canvas>` (`toJpegDataUrl`) before being stored in
  `referenceImages[]`. This keeps the request small for what are typically
  texture/material/furniture references. JPEG has no alpha, so transparent
  source pixels are flattened onto **white**.
- The captured 3D image and the original-capture reference are **never** routed
  through JPEG re-encoding — they stay **PNG (lossless)** so the detail-recovery
  mechanism isn't undermined.
- Each attachment shows its **index badge** (1, 2, 3…). The prompt tells the
  model the references are "attachment 1 to N", so an instruction like *"use the
  material from attachment 3"* resolves to the 3rd reference image.

### Why base64?

Both backends carry the image **inside a JSON body** (Gemini `inlineData.data`,
Flux `input_image`). JSON is text and cannot hold raw binary, so the bytes must
be base64-encoded — this adds ~33 % over the raw byte size. Sending raw bytes
would require moving them out of the JSON body (e.g. Gemini's Files API). See
the discussion in the project notes; not currently implemented.

---

## 4b. Region edits (marquee)

The **"▱ Mark region"** button lets the user draw a polygon on the working image
to confine an edit (e.g. *"put a sofa here"*, *"change this wall to brick"*). All
of this lives in `nanoBanana.html`:

- The polygon is stored in **image-native pixel coordinates** and projected to
  the on-screen `object-fit: contain` rect on every redraw, so it stays aligned
  when the palette is resized. Drawn on a `<canvas>` overlay that only captures
  clicks while drawing (so the compare slider keeps working otherwise).
- `buildRegionImages()` produces two artifacts at native resolution:
  - **marker composite** — the working image with the region painted (magenta
    outline + translucent fill);
  - **mask** — white polygon on black.
- The region is cleared after a successful render and on capture / reset / undo.

**The payload depends on the active backend** (`ARUTILS.GetProvider()`), because
the APIs differ in how they accept a region:

| Backend | What's sent for a region edit | Region precision |
|---------|-------------------------------|------------------|
| **Gemini** | the **marker-composited** working image + a `REGION_NOTE` telling the model to edit only inside the magenta outline (Gemini has no mask input) | soft / approximate |
| **Flux** | the **clean** working image + the **mask** (slot 4) → **FLUX.1 Fill** endpoint | pixel-exact |
| **Local (SD WebUI)** | the **clean** working image + the **mask** (slot 4) → `img2img` **inpaint** | pixel-exact |

> **Reference-material limitation.** Attachments are sent to **Gemini only**, so
> *"change the material in the marked area to attachment 3"* resolves on Gemini
> (multi-image), but **not** on Flux/Local — their mask APIs have no
> reference-image input (that needs IP-Adapter/ControlNet). Flux and Local do
> precise **text-driven** region edits.

---

## 5. Formats & sizes at a glance

| Stage | Format | Size |
|-------|--------|------|
| 3D capture (input / working / original) | PNG, lossless | = 3D window pixels (variable, can be > 1 MP) |
| Attachments (upload) | JPEG q0.9 | re-encoded; much smaller than source PNG |
| Request body | base64 in JSON | ~+33 % over raw bytes |
| Model output | PNG (Gemini) | ~1024 px on the long edge (~1 MP), model-fixed |

The model normalizes its **output** to ~1 MP regardless of input size — which
is exactly why the original capture is re-sent at full PNG quality on every
refinement.

---

## 6. Provider A — Gemini (`generateContent`)

Single synchronous request to
`https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent`.

```mermaid
sequenceDiagram
    participant Page as nanoBanana.html
    participant Panel as NanoBananaPanel (Render)
    participant Gem as GeminiClient
    participant API as Gemini API
    Page->>Panel: ARUTILS.Render(args)
    Panel->>Gem: RenderImage(key, prompt, working, original, refs, history)
    Gem->>Gem: build JSON: contents[].parts[] = [text, inlineData(working), inlineData(original?), inlineData(ref)…]
    Gem->>API: POST …:generateContent  (header x-goog-api-key)
    API-->>Gem: candidates[0].content.parts[].inlineData.data (base64)
    Gem-->>Panel: data:image/png;base64,… (or error)
    Panel-->>Page: resolve Promise
```

- **Auth:** the key travels in the `x-goog-api-key` **header** (not the URL), so
  it doesn't leak into request logs.
- **Body:** one `user` turn whose `parts` are the prompt text followed by each
  image as `inlineData { mimeType, data }`. The image order is: working image →
  original capture (if any) → attachments, in order. Each image keeps its own
  mime type (`image/png` for captures, `image/jpeg` for attachments).
- **Prompt:** user instruction, plus a note about the original-capture
  reference, plus the "attachment 1..N" numbering note, plus fixed quality rules
  (`kDefaultBaseRules`) appended last so the model weights them most.
- **Response:** the first `inlineData.data` found in `candidates[0]` is the
  result. The code wraps it as `data:image/png;base64,…`.
  > Note: the response's real `mimeType` is ignored and the wrapper is
  > hardcoded to `image/png`. Today the model returns PNG, so this is correct.

---

## 7. Provider B — Flux (FLUX.1 Kontext, async)

BFL's API is **asynchronous**: submit a task, poll until ready, then download.
Flux sends **only the working image** — attachments are not transmitted.

```mermaid
sequenceDiagram
    participant Panel as NanoBananaPanel (Render)
    participant Flux as FluxClient
    participant API as BFL API
    participant CDN as Delivery URL
    Panel->>Flux: FluxRenderImage(key, baseUrl, model, prompt, working, history)
    Flux->>API: POST {base}/v1/{model}  {prompt, input_image(b64), output_format:png}  (header x-key)
    API-->>Flux: { id, polling_url }
    loop every 1.5s, up to 120s
        Flux->>API: GET polling_url  (header x-key)
        API-->>Flux: { status, result.sample? }
    end
    Note over Flux: status == "Ready"
    Flux->>CDN: GET result.sample (binary PNG)
    CDN-->>Flux: image bytes
    Flux-->>Panel: data:image/png;base64,… (or error)
```

- **Submit:** `POST {baseUrl}/v1/{model}` with `{ prompt, input_image, output_format:"png" }`.
  `input_image` is the working image's base64 payload (data-URL prefix stripped).
  Auth via the `x-key` header (omitted for a keyless self-hosted server).
- **Region edit (Fill):** when a **mask** is supplied (slot 4), the submit goes to
  `POST {baseUrl}/v1/flux-pro-1.0-fill` with `{ prompt, image, mask, output_format }`
  instead — a precise masked inpaint (white = repaint). The poll/download steps are
  identical. The "keep geometry" prompt cue is dropped for fills.
- **Poll:** `GET polling_url` (or `{base}/v1/get_result?id=…`) every 1.5 s for up
  to 120 s. `Ready` → done; `Pending`/`Processing`/`Queued` → keep polling; any
  other status (moderated / error / not found) → fail.
- **Download:** `GET result.sample` (a signed delivery URL, no auth) returns the
  raw image bytes, which are base64-encoded and wrapped as a PNG data URL.
- The generic `HttpDo()` helper parses any absolute URL into host + path and
  reads the body as raw bytes, so it handles cloud and self-hosted hosts and
  both JSON and binary responses.

---

## 7b. Provider C — Local SD WebUI (Forge / A1111, `img2img`)

A single **synchronous** call to a self-hosted Stable-Diffusion WebUI
(AUTOMATIC1111 or Forge). Like Flux, it sends **only the working image** —
attachments are not transmitted. The model used is whatever checkpoint is loaded
in the WebUI; the add-on does not switch it.

```mermaid
sequenceDiagram
    participant Panel as NanoBananaPanel (Render)
    participant Web as WebUIClient
    participant API as SD WebUI (/sdapi)
    Panel->>Web: WebUIRenderImage(baseUrl, prompt, working, mask, history)
    Web->>Web: read PNG width/height; build JSON {init_images, prompt, denoising_strength, steps, width, height, [mask, inpaint_*]}
    Web->>API: POST {baseUrl}/sdapi/v1/img2img  (optional Basic auth)
    API-->>Web: { images: ["<base64 PNG>"] }
    Web-->>Panel: data:image/png;base64,… (or error)
```

- **Endpoint:** `POST {baseUrl}/sdapi/v1/img2img`; default base
  `http://127.0.0.1:7860`. The WebUI must be started with the API enabled
  (`--api`).
- **Body (whole-image):** `init_images[0]` = the working image's base64 payload;
  `prompt` (with history context + a realism suffix); `denoising_strength` (0.6);
  `steps` (30); and `width`/`height` read from the PNG header so the WebUI doesn't
  fall back to its 512×512 default and downscale.
- **Body (region edit / inpaint):** when a **mask** is supplied (slot 4), the
  call becomes a precise inpaint — adds `mask`, `inpainting_mask_invert: 0`
  (white = repaint), `inpainting_fill: 1`, `inpaint_full_res: true` (+ padding),
  `mask_blur: 4`, and raises `denoising_strength` to **0.75** in the masked area.
  Only the marked pixels change; the realism suffix drops the "keep geometry" cue
  so edits like "put a sofa here" aren't fought.
- **Auth:** optional HTTP Basic — supply credentials inline in the URL
  (`http://user:pass@host:port`) for a server launched with `--api-auth`. The
  client moves the userinfo into an `Authorization: Basic` header.
- **Response:** `images[0]` is a base64 PNG, wrapped as a PNG data URL. Clear
  messages are surfaced for 404 (API not enabled) and 401 (bad credentials).
- **Note:** unlike Flux/BFL, this targets the WebUI's own API — it does *not*
  speak ComfyUI's workflow API. Tuning (denoising, steps, inpaint params) is
  currently fixed in `WebUIClient.cpp`.
- **Setup:** see [Local-A1111-Setup.md](Local-A1111-Setup.md) for installing and
  configuring the server.

---

## 8. Result handling

`ARUTILS.Render(...)` resolves with either a `data:` URL or a string starting
with `"ERROR: "`. In `doRender().then(...)` the page:

1. On error → shows the message in the status line and re-enables Send.
2. On success → records the prompt in history, pushes the result onto the edit
   stack (bounded by `maxHistory`), updates the before/after compare slider,
   enables **Save**, and clears the prompt for the next instruction.

**Save** (`ARUTILS.SaveImage`) decodes the data URL and writes the bytes to a
file chosen via a native dialog, picking the extension from the data-URL mime.

---

## 9. Settings & persistence (brief)

Settings (provider, Gemini key/model, Flux key/URL/model, Local URL) are stored
as one versioned blob (v5) via `ACAPI_SetPreferences` / `ACAPI_GetPreferences`.
**This data is saved into the project (`.pln`) file** — it is per-project, and
the API keys travel inside saved projects. See `Src/NanoBanana/Settings.cpp`.

`IsConfigured()` gates rendering: Gemini needs an API key; Flux needs a key (or a
non-default server URL for keyless self-hosting); Local is always considered
configured because it falls back to `http://127.0.0.1:7860`.
