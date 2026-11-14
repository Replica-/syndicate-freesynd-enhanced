# freesynd-enhanced

**freesynd-enhanced** is a fork of [FreeSynd](https://freesynd.sourceforge.io/), a free, cross-platform
reimplementation of the engine of Bullfrog's classic game *Syndicate*.

All credit for the engine goes to the FreeSynd team (see [AUTHORS](AUTHORS)). For the
upstream documentation, controls and known issues, see [README](README) and
[INSTALL](INSTALL), or visit the project website: https://freesynd.sourceforge.io/

## What's different in this fork

- **Windows / VS Code development setup**: build tasks, debug launch configurations,
  and a self-contained dev configuration in [dev/](dev/) so the game runs straight
  from the build tree.
- **Runtime DLLs copied automatically**: the bundled SDL2 DLLs are copied next to
  `freesynd.exe` and `fs-editor.exe` after each build on Windows.
- **Audio fixes**:
  - Music (OPL/MIDI through libADLMIDI) was garbled on surround-sound devices, because
    SDL opened the device with more than 2 channels while the synth always writes
    stereo. The mixer is now kept at stereo, and the synth runs at the frequency the
    device actually provides.
  - Sound effects (Creative VOC samples from the original data) are now decoded
    by FreeSynd and resampled with `SDL_AudioStream`. SDL_mixer's VOC loader could
    write out of bounds while resampling and crash at startup, and it failed to load
    some samples.
- **Shots from mid-body**: every weapon except the flamer fired from the shooter's
  feet, so the line of fire started at floor level. The floor itself could block it,
  and shots sometimes landed under the agent's feet. Shots, the range limit, and the
  line-of-fire checks (AI sight, "target in range", following a target) now all
  start at half the shooter's height, as the flamer and the aim point already did.
- **`--intro` option**: plays the intro movie even if it has already been played.
- **Headless runs**: if no hardware renderer is available (for example with
  `SDL_VIDEODRIVER=dummy`), the game falls back to SDL's software renderer
  instead of failing to start.

## Building on Windows

Prerequisites:

- Visual Studio 2022 or Build Tools 2022 with the *Desktop development with C++*
  workload (this includes MSVC, CMake and Ninja)
- Python 3 and [Conan 2](https://conan.io/) (`pip install conan`, then
  `conan profile detect`), with `conan` on your `PATH`

From a command prompt:

```
dev\build.cmd            # Debug build
dev\build.cmd release    # Release build
```

Or use the CMake presets directly from a *x64 Native Tools* prompt:

```
cmake --preset ninja-msvc-x64-debug
cmake --build --preset build-ninja-msvc-x64-debug
```

For other platforms, see [INSTALL](INSTALL).

## Game data

FreeSynd needs the data files of the original game, which are not included.
Put your copy of *Syndicate* in `game_data/` (ignored by git). The dev configuration
([dev/freesynd.ini](dev/freesynd.ini)) expects the layout of the GOG release, with the
original data in `game_data/SYNDICAT/DATA`. If your copy is laid out differently,
edit `data_dir` in that file.

The GOG data differs from the reference checksums for the mission briefing
files, so the startup data check is turned off in
[dev/user/user.conf](dev/user/user.conf) (`test_data = false`).

## Running and debugging in VS Code

1. Install the recommended extensions (C/C++ and CMake Tools).
2. Press <kbd>F5</kbd> and pick one of the launch configurations:
   - **FreeSynd (Debug)**, **(Release)**, **Intro** (always plays the intro movie,
     which otherwise only plays on the first run), **Jump to mission** (asks for a
     mission number), or **No sound**
   - **FS Editor (Debug)**
   - **Tests: kernel** / **Tests: engine**

Each configuration builds first, then runs from the workspace root with
`-i dev -u dev/user`. Saves go to `dev/user/save/`.

To run it by hand:

```
build\ninja-msvc-x64-debug\game\freesynd.exe -i dev -u dev/user
```

To run it headless (no window, no audio), for example for automated smoke tests,
use SDL's dummy drivers together with `-n`. The game falls back to the software
renderer when no hardware renderer is available:

```
set SDL_VIDEODRIVER=dummy
set SDL_AUDIODRIVER=dummy
build\ninja-msvc-x64-debug\game\freesynd.exe -i dev -u dev/user -n
```

The **FreeSynd (Debug) - Headless, no sound** launch configuration does the same.

## License

FreeSynd is released under the GNU General Public License, version 3.
See [COPYING](COPYING).
