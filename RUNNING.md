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

## Experimental larger viewports

On the `feature/native-resolution` branch, try:

```sh
./bumpkins --game-dir "/path/to/game" --skip-intro --resolution 1280x720
```

Resize the window or use Alt+Enter to enlarge the presentation. This option
increases the internal viewport, showing more map with the original sprite and
font sizes. It overrides the extracted resolution in memory and does not patch
the executable, rewrite the asset file or change saved games. Without it, the
resolution recorded during extraction is used (normally 640x480).

The experimental range is width 640–1920 (a multiple of four) and height
480–1080. Linux probes reached mission 1, dismissed its briefing, selected an
actor and submitted a movement command at 640x480, 1280x720, 1280x960 and
1920x1080. These checks are not full mission playthroughs.

The toolbar extends across the viewport with profession buttons at the right
edge. Weather, portrait animations, stock text and tooltips follow their screen
anchors. Menus, loading screens and briefings keep their original pixel size
and use a centered 640x480 canvas. Moved toolbar buttons and menu hover areas
use the same coordinates as their drawing.

Keep larger viewports opt-in while playtesting the remaining screens and
missions. Independent UI scaling is not implemented; choose a smaller internal
viewport if the original controls become too small. Window resizing continues
scaling the chosen viewport with black borders, without changing its dimensions
mid-mission.
