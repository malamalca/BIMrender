# Tools

Resource compiler used by the Archicad 30 (and newer) builds.

Up to Archicad 29 the API Development Kit shipped `CompileResources.py` in
`Support/Tools`, and `CMakeLists.txt` called it straight from the DevKit. The
Archicad 30 DevKit no longer ships it there: it is now part of the add-on
project template, so every add-on carries its own copy. These files are that
copy, taken from
`API Development Kit 30.3000/Examples/<any example>/Tools`:

- `CompileResources.py`
- `JsonToGrcConverter/` (imported by `CompileResources.py`; only actually used
  for JSON based resources, which this add-on does not have)

The command line changed with Archicad 30 — it now also takes the add-on name,
the default language code, the Archicad version and the DevKit build number,
and its DevKit path points at the `Support` subfolder — so `CMakeLists.txt`
keeps using the DevKit's own script for Archicad 27-29 and this copy for
Archicad 30 and newer.

The build number comes from `VersionAndBuildNumber.txt` in the DevKit root.
`CompileResources.py` needs it to pick the right `ResConv` options (`-py` and
`-sc`, which generate the Dark Mode icons); without it the script falls back to
an `APIDevKitLinks.json` that this copy does not carry, so `CMakeLists.txt`
fails with a clear message instead of letting the build get that far.

## Local changes

`CompileResources.py` has three deliberate deviations from the stock file, each
marked with a comment in the code.

1. `CreateNativeResourceFileWithIncludes`: the stock version generates a wrapper
   `.rc2` that `#include`s *every* `.rc2` in the resource objects folder and then
   appends `AddOnMain.rc2`. That does not work here, because
   `RFIX.win/AddOnMain.rc2` already `#include`s exactly those generated `.rc2`
   files by name, so every resource in them would be defined twice.
   `AddOnMain.rc2` is the authoritative list — the same one the Archicad 27-29
   builds use — so it is handed to `rc` unchanged.

2. `CompileNativeResource` passes `/d AC_API_VERSION=<version>` to `rc`. The stock
   project template has no version resource of its own in `RFIX.win` — it
   generates one from `Tools/VersionInfo.rc.in` through `CMakeCommon.cmake`, with
   `PRODUCTVERSION 0,<Archicad version>,0,<GS build number>`. This project does
   not use `CMakeCommon.cmake` at all, keeps its version resource in
   `RFIX.win/AddOnMain.rc2` and builds for Archicad 27-30 from one
   `CMakeLists.txt`, so the Archicad version reaches that resource as an `rc`
   define instead. The Archicad 27-29 builds use the DevKit's own script, which
   does not pass the define, and keep the old value through the `#else` branch in
   `AddOnMain.rc2`.

3. `RunResConv` passes both `RFIX/Images` and `RFIX` as `ResConv` resource
   search paths (`-p`, `;`-joined). The stock file passes only `RFIX/Images`, so
   a file a `.grc` embeds by name from `RFIX` itself — `nanoBanana.html`, via
   `'DATA' ID_DATA_NANOBANANA_HTML` in `RFIX/BIMrenderFix.grc` — is never
   found and the whole resource compilation fails with "Can't open file:
   nanoBanana.html". A bare name cannot resolve on its own either, because the
   script `chdir`s to its own folder. The Archicad 29 DevKit script passed both
   folders; the Archicad 30 template dropped the second one.

Why the version resource matters at all: Archicad 30 reads the Archicad version
an add-on was built for from the `PRODUCTVERSION` field of the Win32 version
resource. Archicad 27-29 generated no version resource and did not look at one;
an add-on that reaches Archicad 30 with a missing or wrong `PRODUCTVERSION` is
rejected with "the file is either not an add-on or an outdated add-on that
cannot be used with this version of Archicad".

When updating to a newer DevKit, re-copy both items and re-apply all three
changes.
Check `Tools/VersionInfo.rc.in` in the new DevKit's examples for changes to the
`PRODUCTVERSION` layout.
