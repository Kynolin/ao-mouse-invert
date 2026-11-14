# ao-mouse-invert

Inverted mouse-look (Y axis) for the community-developed **Anarchy Online** game client, until it gets a built-in option.

Drop one DLL into the game folder. Hold the right (or left) mouse button to steer the camera, and up/down are inverted. Normal cursor movement in menus and the UI is not affected.

> [!WARNING]
> **This is an unofficial workaround, not a proper feature.**
> - It is not made by, endorsed by, or affiliated with Funcom or the client's developers.
> - It works by intercepting how the game reads the mouse. A future client update may break it, or make it unnecessary. When the client ships its own invert option, **remove this** (see [Uninstall](#uninstall)), or the two will cancel each other out.
> - Please check with the client developers that they're OK with it before using it, and **don't report camera bugs to them while it's installed**.
> - **Never use this with games that have anti-cheat.** DLL proxying and function hooking are exactly what anti-cheat looks for, and you could be banned.
> - Provided as-is with no warranty (see [LICENSE](LICENSE)). Use at your own risk.

## Install

1. Download the latest `ao-mouse-invert-vX.Y.Z.zip` from the [Releases](../../releases) page (or [build it yourself](#building-from-source)).
2. Work out whether the game client is 64-bit or 32-bit:
   - Start the game, open **Task Manager → Details**, right-click any column header → **Select columns** → tick **Platform**.
   - **64-bit** → use `x64\dinput8.dll`. **32-bit** → use `x86\dinput8.dll`.
3. Copy that `dinput8.dll` into the **same folder as the game's `.exe`**.
4. *(Optional)* Copy `dinput8_invert.ini` there too, to change which buttons invert (see below).
5. Start the game and drag the camera with the right mouse button.

> [!NOTE]
> If the game folder **already contains** a `dinput8.dll`, stop. The game ships its own, and this one would replace it. Open an issue instead.

## Configure

Edit `dinput8_invert.ini` (next to the DLL), then restart the game:

```ini
[invert]
buttons=right
```

| Value   | Y is inverted when…                         |
|---------|---------------------------------------------|
| `right` | dragging the camera with the right button (default) |
| `left`  | dragging the camera with the left button    |
| `both`  | dragging with either button                 |
| `any`   | the camera is moving, regardless of button  |

Without the `.ini` file, the default is `right`.

## Uninstall

Delete `dinput8.dll` (and `dinput8_invert.ini` / `dinput8_invert.log`) from the game folder. Nothing else is installed or changed on your system.

## Troubleshooting

Every time the game starts, the DLL writes `dinput8_invert.log` next to itself.

| What you see | Likely cause |
|---|---|
| No log file appears | The DLL isn't being loaded: wrong folder, or wrong 32/64-bit version. |
| Log shows `hooks FAILED` or `hook … failed` | Something else (an overlay, another mod) is hooking the same functions. Try disabling overlays. |
| Log shows `buttons=…` you didn't expect | Check spelling in the `.ini`; unknown values fall back to `right`. |
| No `camera raw mouse ON` lines when dragging | The client changed how it reads the camera. Please open an issue and attach the log. |

A healthy log looks like:

```
[826250] invert proxy v3 loaded (64-bit)
[826250] buttons=both
[826296] hooks enabled
[859109] camera raw mouse ON   (L=0 R=1)
[859703] camera raw mouse OFF  (L=0 R=0)
```

**Antivirus warnings:** proxy DLLs that hook Windows functions sometimes trigger false positives. The full source is here; build it yourself if you'd rather not trust a downloaded binary.

## How it works

1. **DLL proxy.** Windows loads a `dinput8.dll` from the game's own folder before the system copy. This DLL loads the real `System32\dinput8.dll` and forwards every DirectInput call to it unchanged.
2. **Raw input hook.** On load, it uses [MinHook](https://github.com/TsudaKageyu/minhook) to hook three `user32.dll` functions *inside the game process only*: `GetRawInputData`, `GetRawInputBuffer` and `RegisterRawInputDevices`.
3. **Camera-only inversion.** The client registers for raw mouse input only while you're dragging the camera, and unregisters when you let go. Raw mouse packets therefore only exist during camera drags, so negating their vertical movement (`lLastY`) inverts the camera without touching the UI cursor.
4. **Button gating.** Inversion only applies while the configured button is held (`GetAsyncKeyState`).

No drivers, no admin rights, no system-wide hooks, and nothing persists once the DLL is deleted.

## Building from source

The source is one C file (`src/dinput8.c`) plus MinHook, built with CMake. Clone with submodules so MinHook comes along:

```sh
git clone --recursive https://github.com/Kynolin/ao-mouse-invert.git

cd ao-mouse-invert
# already cloned without --recursive?
git submodule update --init
```

### Option A: Windows with Visual Studio (recommended)

1. Install [Visual Studio Build Tools](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio) (or Visual Studio Community) with the **"Desktop development with C++"** workload. This includes CMake.
2. Open **"Developer PowerShell for VS"** from the Start menu, `cd` into the repo, and run:

```powershell
# 64-bit
cmake -B build-x64 -A x64
cmake --build build-x64 --config Release
# → build-x64\Release\dinput8.dll

# 32-bit
cmake -B build-x86 -A Win32
cmake --build build-x86 --config Release
# → build-x86\Release\dinput8.dll
```

The DLL is statically linked, so players don't need the Visual C++ Redistributable.

### Option B: Linux, WSL or macOS (MinGW-w64 cross-compile)

Install MinGW-w64 and CMake (Debian/Ubuntu/WSL: `sudo apt install mingw-w64 cmake`; macOS: `brew install mingw-w64 cmake`), then:

```sh
# 64-bit
cmake -B build-x64 -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-x64
# → build-x64/dinput8.dll

# 32-bit
cmake -B build-x86 -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-i686.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-x86
# → build-x86/dinput8.dll
```

### Automated builds and releases (GitHub Actions)

[`.github/workflows/build.yml`](.github/workflows/build.yml) builds both DLLs on every push and pull request; download them from the run's **Artifacts**. To publish a release with a ready-to-use zip, push a version tag:

```sh
git tag v1.0.0
git push origin v1.0.0
```

## Project layout

```
src/dinput8.c            the proxy + hooks (all the logic lives here)
src/dinput8.def          export list matching the real dinput8.dll
dinput8_invert.ini       example settings file
cmake/                   MinGW cross-compile toolchain files
third_party/minhook/     MinHook (git submodule, BSD 2-Clause)
.github/workflows/       CI build + release
```

## Credits and license

- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu, BSD 2-Clause (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
- This project is [MIT licensed](LICENSE).

*Anarchy Online is a trademark of Funcom. This project is not affiliated with Funcom.*
