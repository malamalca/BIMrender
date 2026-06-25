# BIMrender

AI rendering add-on for Archicad. Captures the current 3D view and edits it with
an image model, with optional region (marquee) edits.

Backends:
- **Google Gemini** (`generateContent`) — multi-image; supports reference
  "attachment" images; soft region marker.
- **Flux** (Black Forest Labs) — Kontext for whole-image edits, **Fill** endpoint
  for precise masked region edits.
- **Local** — self-hosted SD WebUI (AUTOMATIC1111 / Forge) `img2img` / inpaint.

The provider, keys and model are configured from the palette's ⚙ settings.

## Build (Windows)

```
build.bat
```
or via CMake presets (`ac29-INT`, …). The add-on name is taken from the folder
name, so the output is `BIMrender.apx`.

## Docs

See [Docs/NanoBanana-Upload-Processing.md](Docs/NanoBanana-Upload-Processing.md)
for how the pipeline works, [Docs/Local-A1111-Setup.md](Docs/Local-A1111-Setup.md)
for the local server, and [Docs/ideas.md](Docs/ideas.md) for the backlog.
