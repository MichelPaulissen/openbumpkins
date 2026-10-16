# Running a build

These packages contain the engine, not the original game data. Use a writable
copy of your Beasts & Bumpkins game directory.

First extract the embedded resources from your original or bb-patch executable:

```sh
python3 extract_native_assets.py --game-dir "/path/to/game"
```

This creates `bumpkins-assets.bin` alongside your game files. The engine needs
that file and the original data files; it does not execute `BEASTS.EXE` or the
original DLLs. From a source checkout, the extraction script is under `tools/`.

On Linux or macOS:

```sh
./bumpkins --game-dir "/path/to/game" --skip-intro
```

On Windows, use Python's `py` launcher for the extraction command and start the
game from PowerShell:

```powershell
py extract_native_assets.py --game-dir "C:\Games\Beasts"
.\bumpkins.exe --game-dir "C:\Games\Beasts" --skip-intro
```

Linux builds target Ubuntu 24.04 or a compatible system and require SDL2
(`sudo apt install libsdl2-2.0-0` on Ubuntu). Apple Silicon macOS builds include
their SDL libraries and target macOS 15 or later. Windows x86-64 builds include
their runtime DLLs; keep those beside `bumpkins.exe`. Python 3.9 or later is
needed only for resource extraction. Mac builds are ad-hoc signed, not notarized.

Alt+Enter toggles fullscreen. macOS also supports Control+Command+F. Resizing
preserves the game's aspect ratio with black borders. Saves and configuration
are stored in the writable game directory.
