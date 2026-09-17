# Region Edits (Marquee) — Prompting Guide

How marquee-confined edits work for each backend, and how to write prompts that
keep the change inside the marked area.

For the data-flow details (payload slots, endpoints) see
[NanoBanana-Upload-Processing.md](NanoBanana-Upload-Processing.md), §4b.

---

## The two families

The marquee produces two artifacts (`buildRegionImages()` in
`RFIX/nanoBanana.html`):

- a **marker composite** — the working image with a magenta outline +
  translucent fill painted on;
- a **mask** — white polygon on black.

Which one is sent, and how the region is enforced, depends on the backend:

| Backend | Region mechanism | Precision | Sends attachments? |
|---------|------------------|-----------|--------------------|
| **Gemini** | visible marker burned into the image + text note | soft / approximate | yes |
| **Flux** | binary **mask** → FLUX.1 Fill endpoint | pixel-exact | no |
| **Local (SD WebUI)** | binary **mask** → `img2img` inpaint | pixel-exact | no |

Gemini has **no mask input**, so it uses *visual-marker* prompting. Flux and
Local accept a real mask, so they do *masked inpainting* — only the marked
pixels can change, and the model never sees the marker.

---

## Gemini — visual-marker prompting

The `generateContent` API has no mask input, so the marquee is burned into the
image as a magenta outline and the model is *told* to stay inside it.

What the add-on sends (see `doRender()`):

1. The **marker composite** (not the clean image) — magenta outline +
   translucent fill at native resolution.
2. The user's prompt with `REGION_NOTE` appended:

   > "Apply your edit ONLY inside the area enclosed by the magenta outline;
   > keep everything outside that outline exactly as in the input. The magenta
   > outline is only a guide — do not draw it in the result."

3. `GeminiClient` appends `kDefaultBaseRules` (photorealism + "preserve all
   geometry") **last**, so the constraints carry the most weight.

That three-part structure — **(a)** a visible marker, **(b)** an "edit only
inside / keep everything else identical" clause, **(c)** a "don't render the
marker" clause — is the recommended pattern for Gemini image editing.

**Prompt tips:**

- **Name the content semantically, not just spatially.** *"Replace the wall
  inside the marked area with exposed brick"* is far more reliable than *"make
  this brick"*. The marker narrows **where**; the description tells the model
  **what object** the edit applies to.
- **Describe the desired result, not the removal.** *"An empty paved terrace
  where the marked furniture is"* beats *"delete the furniture"* — the model
  needs to know what fills the region.
- **One edit per iteration.** Stacking several changes makes edits bleed outside
  the outline.
- **Don't restate the region mechanics** — `REGION_NOTE` already does it;
  duplicating it dilutes the instruction.
- **Attachments work here.** *"Change the material in the marked area to
  attachment 3"* resolves on Gemini (multi-image), but not on Flux/Local.

**Precision caveat:** Gemini regenerates the entire frame at ~1 MP, so pixels
outside the marquee drift slightly. For pixel-exact edges the standard trick is
**client-side compositing** — after the render, paste the output back into the
original only where the mask is white. The page already keeps `lastRegionMask`
for the precise backends, so a canvas composite in the render callback would
give Gemini hard edges at zero API cost.

---

## Flux — FLUX.1 Fill (masked inpaint)

For a region edit the add-on sends the **clean** working image plus the
**mask** (slot 4) to the `/v1/flux-pro-1.0-fill` endpoint, as
`{ prompt, image, mask, output_format }` (`FluxClient.cpp`). White mask =
repaint. The result is **pixel-exact**: only the marked pixels can change, and
the marker is never sent.

Prompt handling differs from a whole-image pass: the *"keep all architectural
geometry"* cue is **dropped** for fills (it would fight edits like "put a sofa
here"); only a realism cue is appended:

> ". Produce a sharp, photorealistic result."

**Prompt tips:**

- **Describe only what belongs in the masked area.** The mask already handles
  *where*; the prompt is purely *what*. *"A grey linen three-seat sofa"* is
  enough — no need to say "in the selection".
- **No `REGION_NOTE`, no marker language.** There is no marker in the image;
  spatial hedging just confuses the model.
- **Text-driven only — no attachments.** Flux Fill has no reference-image input
  (that needs IP-Adapter/ControlNet), so *"use attachment 3"* does **nothing**.
  Describe the material/object in words instead.
- **Fill removal by describing the replacement surface**, e.g. *"smooth polished
  concrete floor"* over a masked object.

---

## Local (SD WebUI / A1111 / Forge) — `img2img` inpaint

For a region edit the add-on sends the **clean** working image plus the
**mask** to `POST {baseUrl}/sdapi/v1/img2img` as an inpaint
(`WebUIClient.cpp`). It adds `mask`, `inpainting_mask_invert: 0` (white =
repaint), `inpainting_fill: 1`, `inpaint_full_res: true`
(`inpaint_full_res_padding: 32`), and `mask_blur: 4`. Result is
**pixel-exact**: only marked pixels change.

- `inpaint_full_res: true` renders the masked region at full resolution for
  sharper detail; `mask_blur: 4` softens the seam so the patch blends.
- `denoising_strength` is **0.6** (fixed in `WebUIClient.cpp`; same as the
  whole-image pass). Higher = the masked area departs further from the original;
  lower = subtler change. Tuning is currently hard-coded, not exposed in the UI.
- The realism suffix drops the "keep geometry" cue for inpaint, same rationale
  as Flux.

**Prompt tips:**

- **Describe only the masked content**, as with Flux. The clean surroundings are
  preserved automatically.
- **The active checkpoint matters.** The add-on does not switch models — the
  loaded WebUI checkpoint determines style and quality. An architectural /
  photoreal checkpoint gives the best results.
- **Text-driven only — no attachments.** Same limitation as Flux.
- If an edit is too weak or too strong, the lever is `denoising_strength` in
  `WebUIClient.cpp` (not the prompt).

---

## Quick rule of thumb

- **Gemini:** describe **what** *and* imply **where** (the marker guides it);
  attachments allowed; edges are soft.
- **Flux / Local:** describe **only what** goes in the mask; **where** is exact;
  no attachments; text-only.
