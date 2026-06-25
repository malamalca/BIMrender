# NanoBanana — Ideas / Backlog

Candidate features for the AI rendering part. Chosen to fit the existing
architecture (capture 3D → edit, region masks, Gemini / Flux / Local backends,
attachments, compare slider, history/undo, save).

Effort is rough: **S** = small, **M** = medium, **L** = large.

## High value, good fit

1. **Variations picker (generate N, pick the best).** — *M, all backends.* **Top pick.**
   Fire 2–4 renders for one prompt and show them in a small grid to choose from.
   Fixes "the result wasn't what I wanted / it's non-deterministic": compare
   options instead of one-shot + retry. Flux/Local take a `seed` for clean
   variation; Gemini just re-calls.

2. **Architectural style/preset buttons.** — *S, all backends.*
   One-click chips above the prompt: Dusk · Twilight exterior · Overcast ·
   Winter/snow · White model · Sketch · Section-cut render · Add entourage
   (people/trees/cars). Each prepends a tuned prompt. Cheap, high value for arch
   viz, and standardizes prompts so fewer `IMAGE_OTHER`-style failures.

3. **Brush mask + multi-region + "remove object" mode.** — *M, builds on existing mask code.*
   Extend the marquee with a freehand brush (faster than polygon for organic
   shapes), an eraser, multiple regions, and a one-click Remove (inpaint fill
   with an empty prompt). Makes the precise Flux/Local path far more usable.

## Reliability / UX (low effort, addresses observed pain)

4. **Auto-retry + Cancel.** — *S.*
   Auto-retry once or twice on transient `IMAGE_OTHER` / network blips before
   surfacing the error. Add a Cancel button to abort an in-flight request
   (important for slow Flux polling and local-CPU renders).

5. **Seed control (Flux/Local).** — *S.*
   Show the seed of the last render and let the user lock it, so once a
   composition works you can iterate the prompt without the whole image
   reshuffling.

## Deeper / domain-specific

6. **Upscale the final image.** — *M, Local-first.*
   Outputs are ~1 MP (model-capped). Add an upscale pass — Local has
   `/sdapi/v1/extra-single-image`, or re-run img2img at higher res — so the
   deliverable isn't stuck at 1024 px.

7. **Material/reference library + "send result back to Archicad".** — *L, high payoff.*
   A small curated palette of architectural materials (brick, timber, render,
   glass) as quick attachments, and saving the chosen render into the project
   (as a Figure/drawing on the view) instead of only to disk — closing the loop
   with Archicad.

## Suggested sequencing

1. **#2 (presets) + #4 (retry/cancel)** — cheap, immediately smoother.
2. **#1 (variations)** — the bigger UX win.
3. **#3 (brush / remove)** — real capability jump on the mask path.
4. Then #5 (seed), #6 (upscale), #7 (material library / AC integration) as appetite allows.

## Notes / constraints

- Attachments (reference images) are **Gemini-only**; Flux Fill and Local inpaint
  are **text-driven** (no reference-image input without IP-Adapter/ControlNet).
- Region edits: **Gemini** = soft marker; **Flux (Fill)** and **Local (inpaint)**
  = pixel-exact mask.
- Gemini output is ~1 MP PNG, fixed by the model — hence the upscale idea.
- See [NanoBanana-Upload-Processing.md](NanoBanana-Upload-Processing.md) for how
  the pipeline works.
