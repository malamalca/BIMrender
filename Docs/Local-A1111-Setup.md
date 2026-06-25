# Setting up a local AUTOMATIC1111 / Forge server for NanoBanana

NanoBanana's **Local** provider talks to a self-hosted Stable-Diffusion WebUI
over its `img2img` REST API (`/sdapi/v1/img2img`). This guide gets that server
running and wired up.

Two compatible servers — both expose the same API, so either works:

| Server | Repo | Notes |
|--------|------|-------|
| **AUTOMATIC1111** (A1111) | `AUTOMATIC1111/stable-diffusion-webui` | The classic WebUI. Great for SD 1.5 / SDXL checkpoints. |
| **Forge** *(recommended for Flux)* | `lllyasviel/stable-diffusion-webui-forge` | A1111-compatible fork with much better **FLUX.1** support and lower VRAM use. |

> If you want **Flux** quality locally, use **Forge**. If you just want a fast,
> simple photoreal img2img, A1111 with an SDXL checkpoint is fine. NanoBanana
> doesn't care which — it only needs the `img2img` API.

---

## 1. Prerequisites

- **OS:** Windows 10/11, Linux, or macOS.
- **GPU:** an NVIDIA GPU with **≥ 8 GB VRAM** is strongly recommended (12–16 GB+
  for Flux). CPU-only works but a single render can take *minutes*.
- **[Python 3.10.x](https://www.python.org/downloads/release/python-31011/)** —
  3.10 specifically (3.11/3.12 often break the WebUI). Tick *"Add Python to PATH"*.
- **[git](https://git-scm.com/downloads)**.

---

## 2. Install the server

### Windows (A1111)
```bat
git clone https://github.com/AUTOMATIC1111/stable-diffusion-webui
cd stable-diffusion-webui
```
First launch downloads dependencies automatically — just run `webui-user.bat`
once you've done step 3.

### Windows (Forge)
```bat
git clone https://github.com/lllyasviel/stable-diffusion-webui-forge
cd stable-diffusion-webui-forge
```
(Forge also ships a one-click package on its releases page if you'd rather not
use git.)

### Linux / macOS
```bash
git clone https://github.com/AUTOMATIC1111/stable-diffusion-webui
cd stable-diffusion-webui
```

---

## 3. Enable the API (required)

NanoBanana needs the REST API, which is **off by default**. Add the `--api` flag
to the launch arguments.

### Windows — edit `webui-user.bat`
Find the `set COMMANDLINE_ARGS=` line and make it:
```bat
set COMMANDLINE_ARGS=--api
```

### Linux / macOS — edit `webui-user.sh`
```bash
export COMMANDLINE_ARGS="--api"
```

Useful extra flags:

| Flag | Purpose |
|------|---------|
| `--api` | **Enable the REST API (mandatory for NanoBanana).** |
| `--api-auth user:pass` | Require HTTP Basic auth on the API (see §6). |
| `--listen` | Bind to `0.0.0.0` so another machine on the LAN can reach it. **Only with auth + a trusted network.** |
| `--nowebui` | Run the API only, no browser UI (optional). |
| `--xformers` | Faster attention on NVIDIA (A1111). |
| `--medvram` / `--lowvram` | Reduce VRAM use on smaller GPUs. |

Example (small GPU, with auth):
```bat
set COMMANDLINE_ARGS=--api --api-auth archicad:secret --medvram
```

---

## 4. Add a checkpoint (model)

Drop a `.safetensors` checkpoint into:
```
<webui folder>/models/Stable-diffusion/
```
- **SD / SDXL:** any photoreal checkpoint from civitai/HF works for img2img.
- **Flux (Forge):** place the Flux model in `models/Stable-diffusion/` and its
  text encoders (`clip_l`, `t5xxl`) + VAE (`ae.safetensors`) in the matching
  `models/text_encoder` / `models/VAE` folders, then pick the Flux model and set
  Forge's UI mode to **flux**. (Follow Forge's Flux instructions for the exact
  files.)

NanoBanana uses **whatever checkpoint is currently loaded** in the WebUI — it
does not switch models. Load/select your model once in the WebUI before
rendering.

---

## 5. Launch and verify

Start the server:
- Windows: double-click **`webui-user.bat`**
- Linux/macOS: `./webui.sh`

Wait for:
```
Running on local URL:  http://127.0.0.1:7860
```

**Verify the API is up** — open these in a browser:
- `http://127.0.0.1:7860/docs` → the interactive API page (Swagger). If this
  loads, `--api` is active.
- `http://127.0.0.1:7860/sdapi/v1/sd-models` → JSON list of installed
  checkpoints.

Quick `img2img` smoke test (optional):
```bash
curl http://127.0.0.1:7860/sdapi/v1/options
```
A JSON blob means you're good. A 404 on `/docs` means `--api` isn't set.

---

## 6. (Optional) API authentication

If you launched with `--api-auth user:pass`, the API requires HTTP Basic auth.
NanoBanana supplies credentials **inline in the server URL** — put them before
the host:
```
http://user:pass@127.0.0.1:7860
```
The add-on moves the `user:pass` into an `Authorization: Basic` header
automatically. Without `--api-auth`, just use `http://127.0.0.1:7860`.

---

## 7. Point NanoBanana at it

1. Open **NanoBanana → ⚙ Settings**.
2. Select the **Local (Forge / A1111)** provider.
3. Set **WebUI server URL**:
   - same machine: `http://127.0.0.1:7860` (the default)
   - with auth: `http://user:pass@127.0.0.1:7860`
   - another machine: `http://<that-PC-ip>:7860` (server needs `--listen`)
4. Save. Capture a 3D view, type an instruction, press **Send**.

NanoBanana sends your capture to `/sdapi/v1/img2img` with fixed settings —
**denoising strength 0.6, 30 steps**, at the capture's own resolution. The
result quality and style come from the **checkpoint you have loaded**.

### Region edits (inpainting)

The Local provider is the **precise** one for region edits. Use **"▱ Mark
region"** to draw a polygon, then describe the change (e.g. *"put a sofa here"*,
*"change this wall to exposed brick"*). NanoBanana then calls `img2img` in
**inpaint** mode (mask + `inpaint_full_res`, denoising 0.75) so only the marked
pixels change.

- For the cleanest masked results, load an **inpainting-capable checkpoint**
  (a dedicated `…-inpainting` model). Regular checkpoints also work, just with
  slightly softer seams.
- Note: region edits here are **text-driven**. Using an *attachment* image as the
  source material (*"…to attachment 3"*) is **Gemini-only** — plain `img2img` has
  no reference-image input.

---

## 8. Troubleshooting

| Symptom (NanoBanana error) | Cause / fix |
|----------------------------|-------------|
| *"…has no /sdapi/v1/img2img endpoint… launch with --api"* (HTTP 404) | API not enabled. Add `--api`, restart the server. |
| *"…rejected the credentials…"* (HTTP 401) | Server runs with `--api-auth`. Use `http://user:pass@host:port`. |
| *"Could not reach the local WebUI server"* | Server not running, wrong port, or firewall. Confirm `http://127.0.0.1:7860/docs` opens. |
| Render is extremely slow | CPU-only or under-powered GPU. Use a GPU, `--medvram`, or fewer steps (steps are fixed in the client today). |
| Output ignores the prompt / looks unrelated | No suitable checkpoint loaded, or denoising too low for the edit. The loaded model drives the result. |
| Result looks over-changed (geometry drifts) | Denoising 0.6 is fairly strong for some models; this is currently fixed in `WebUIClient.cpp`. |

> **Security note:** the WebUI API has no auth unless you add `--api-auth`. Don't
> expose it to the internet. Use `--listen` only on a trusted LAN and pair it
> with `--api-auth`.

---

## Related

- [NanoBanana — Image Upload & Processing](NanoBanana-Upload-Processing.md) —
  how the request is built and what the local backend sends/receives
  (`Provider C`).
- Tuning (`denoising_strength`, `steps`) currently lives in
  [`Src/NanoBanana/WebUIClient.cpp`](../Src/NanoBanana/WebUIClient.cpp).
