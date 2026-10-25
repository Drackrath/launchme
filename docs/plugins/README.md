# Launchme plugins — SDK v1 (Windows x64)

A plugin is a **ZIP containing a `plugin.json` manifest and a native DLL**. The launcher provides installation,
updates, enabling/disabling, settings, gamepad navigation and native UI rendering.
Plugin code communicates through a small C ABI using UTF-8 JSON messages.

## Usage

**Settings → Plugins**: an “Install plugin from ZIP” button, some spacing, and below it the
“Available plugins” section. Each plugin has a single row: name, description, toggle and **⚙**.
The icon opens a separate page with that plugin's settings and the uninstall option; no keys
or commands are interleaved with the plugin list. Esc/B returns to the list. A plugin without
settings has only the uninstall option on that page.

A plugin newly installed manually from a ZIP is disabled; enable it with the toggle next to its name.
Settings are also available while the plugin is disabled.
Reinstalling a ZIP with the same `id` updates the plugin and keeps
compatible settings and the enabled state. The launcher does not need to be restarted.

`Uninstall` unregisters the extension, calls `deactivate`, unloads the DLL, and removes the
packages and saved settings. The plugin's data/cache directory remains on disk.
An uninstalled plugin does not come back after a restart.

After a build, the packages are in `build/vs/plugin-packages/`:

| Package | Features |
| --- | --- |
| `launchme.howlongtobeat.zip` | block below playtime/achievements in Details, percentage relative to the main story, active-game widget and match-selection drawer; [usage](HOWLONGTOBEAT.md) |

**Packages in `plugin-packages/` next to the EXE are automatically installed and enabled
on first launch** (the file name `<id>.zip` must match the manifest `id`).
The registry records this operation separately for each ID: a later disable or
uninstall is not reverted after a restart. Already installed plugins keep their state.

A newer bundled package (numeric `major.minor.patch`) updates an existing older
version at startup, keeping its settings and enabled state. It does not restore an uninstalled
plugin and does not downgrade a manually installed version.

The `tags` manifest field adds labels to the tile: `widget`, `screen`, `integration`,
`menu`, `context`, `drawer`. The labels are descriptive and have
no side effects: feature registration is still determined solely by `contributions`.

The optional `icon` points to a PNG in the package (up to 256 KiB, path rules as for `entry`), and `iconText`
is a monogram of 1–3 ASCII characters (letters/digits), also used as a fallback. The widget draws the icon
in a circle. Icons are read on the plugin worker and decoded on the image worker.
Rearranging from the drawer header changes the sizes and order of widgets; the layout belongs
to the host, is shared with Guide and requires no changes on the DLL side.

`tools/build.ps1` builds the modules, packs the ZIPs and publishes the finished Runtime to `build/vs`.
The CMake working directory is `build/temp/vs`. Changing only the manifest also refreshes the package.
The `Runtime` install component contains the EXE, the HowLongToBeat ZIP and the miniz/PresentMon/WebView2 licenses.
A custom plugin added to the build via `launchme_plugin(... COMPONENT <name>)` goes into a separate component.

## How to create a new plugin

1. Copy [`sdk/examples/hello`](../../sdk/examples/hello). The example is a standalone
   CMake project, with no dependencies on the launcher's C++ libraries or structures.
2. Change the `id`, name, version and UI extensions in `plugin.json`.
3. Export `launchme_plugin_v1` from the [`sdk/launchme_plugin.h`](../../sdk/launchme_plugin.h) header.
4. Implement the methods described in [API.md](API.md), returning JSON via the `emit` callback.
5. Build an **x64** DLL, pack the manifest and the DLL in the root of the ZIP, and install it in a test
   profile (`LAUNCHME_CONFIG_DIR` set to an absolute path).

```powershell
$vs = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
& "$vs\cmake.exe" -S sdk/examples/hello -B build/vs/plugin-hello -G 'Visual Studio 18 2026' -A x64
& "$vs\cmake.exe" --build build/vs/plugin-hello --config Release
# ZIP: build/vs/plugin-hello/Release/example.hello.zip
```

A real-world example: [`plugins/howlongtobeat`](../../plugins/howlongtobeat). C++ and Rust are both suitable as long as they implement
the same C ABI; C++ compiler compatibility and passing STL types are not required.

## v1 format and limits

- `plugin.json` in the ZIP root, max. 64 KiB. Schema: [manifest.schema.json](manifest.schema.json).
- `apiVersion: 1`, `architecture: "windows-x64"`, `entry` points to the DLL inside the package.
- A new package does not run any code during installation. The host checks the manifest and PE/x64;
  ABI export compatibility and `activate` are checked only when the plugin is enabled. When an
  active plugin is updated, the new module is checked before the change is committed.
- ZIP: max. 64 MiB, 256 entries, 32 MiB per file and 64 MiB in total after extraction;
  stored/deflate, CRC verification. No passwords, links, absolute paths, `..`, ADS,
  Windows device names or duplicate paths (including ones that differ only in letter case).
- Paths inside the ZIP: ASCII letters/digits, `-`, `_`, `.`, `/`; no trailing dots,
  empty segments or `.` segments. Max. 200 characters. The ZIP name and the profile directory may contain Unicode.
- Max. 64 installed plugins, and 32 settings and 12 extensions per manifest.
- UI slots: the first **4** plugin entries in the bottom menu, **3** matching context actions
  and **64** widgets in the scrollable drawer, and the first matching `details` provider in Details.
  A widget may declare `activeGame: true`. Default order: plugin `id`, then manifest order.
  A manifest should declare only the slots it needs; any excess does not fit in the v1 UI.
- `catalog` in v1 supports games identified by Steam AppID; regular plugin lists
  may contain any HTTP(S) links. There is no custom HTML, Direct2D rendering
  by the DLL, frame hooks, hotkeys or public HTTP server.

Native plugins run inside the launcher process with the user's privileges — **this is not
a sandbox**. `library: true` controls what data is passed through the SDK, not the DLL's system
permissions. Invalid JSON/API usage disables the module in question; memory corruption or hung native
code cannot be safely isolated by this host. Authors should limit
I/O time, not block in `deactivate`, and not leave their own threads running when the DLL is unloaded.

## Data and lifecycle

In `%LOCALAPPDATA%\Launchme\plugins` (or the `LAUNCHME_CONFIG_DIR` profile):

```text
registry.dat                    # atomic registry and settings, DPAPI-encrypted
packages/<id>/<revision>/        # immutable ZIP release, manifest and DLL
data/<id>/                      # plugin-owned data/cache
```

Installation extracts into a new staging directory, validates it, and then atomically updates the
registry. A failure during extraction, the ABI check of an active update, or saving leaves the previous
release and settings active. Old release files are deleted after that release's DLL is unloaded.
Stored secrets are DPAPI-encrypted; the UI snapshot only indicates whether they are present.

All extraction, loading and DLL calls happen on a single serialized worker.
The UI uses snapshots; results from an old generation are discarded after the plugin's state changes.
Disabling does not forcibly interrupt a running DLL function: it waits for it to return on the worker,
then calls `deactivate` and unloads the module. The UI remains responsive in the meantime.
During an update, two revisions may be loaded briefly; `activate` should
prepare resources rather than perform irreversible operations.

## Tests

- `native_plugin_package_lifecycle`: real DLLs, ZIP installation/update/removal,
  activation/deactivation, restart, DPAPI, settings, incompatible ABI, rollback,
  unsafe names, corrupted packages and invalid responses.
- `native_settings_store_details_flow`: the real HowLongToBeat ZIP/DLL, native settings
  and the plugin list; a mocked URI executor, without launching a browser.
- `plugin_library_supplement_merge`: deduplication, installation/collection precedence, hiding,
  the not-installed filter, plugin disabling/removal and discarding of stale results.
- `howlongtobeat_search_and_plugin_cache`: parser, cache and the real HowLongToBeat DLL without network access.

OpenAPI is not needed: the interface is not HTTP. The machine-readable contracts are JSON Schema
([manifest](manifest.schema.json), [messages](api.schema.json)) and the C ABI header.
