# freesynd-enhanced

**freesynd-enhanced** is a fork of [FreeSynd](https://freesynd.sourceforge.io/), a free, cross-platform
reimplementation of the engine of Bullfrog's classic game *Syndicate*.

All credit for the engine goes to the FreeSynd team (see [AUTHORS](AUTHORS)). For the
upstream documentation, controls and known issues, see [README](README) and
[INSTALL](INSTALL), or visit the project website: https://freesynd.sourceforge.io/

## What's different in this fork

- **Hardware and software modes**: `render_mode` in `user.conf` (or the `--software`
  command line option) selects the mode. Hardware mode is the default. Software mode
  keeps the original behaviour, for comparison.

  | | Hardware mode (default) | Software mode |
  |---|---|---|
  | Renderer | GPU, with vsync | SDL software renderer |
  | Company colours | the 8 original colours plus 16 new ones that are not limited by the palette | the 8 original colours (a new colour is shown as the closest original one) |
  | Agents' hats in missions | your agents in your company colour, enemy agents in their syndicate's colour | the original dark red for everyone |
  | Lighting in missions | the sun comes from the top left: buildings cast shadows on floors, roads and roofs, slopes facing away are darker, people in shadow are darker. The shading is weaker at dusk and at night (see the time of day below) | flat, as in the original |
  | Lights in missions | lamps and other light fittings in the map, lit windows and neon signs light up their surroundings, pixel by pixel. Lit windows and window glass shine in the dark; stairs never count as lights. Vehicles have headlight cones and red rear lights. They show when the scene is darker than the mission's own look (dusk, night, dawn) | none |
  | Muzzle flashes | firing lights up the area around the shooter, coloured by weapon (yellow for guns, red for the laser, blue for the Gauss gun, orange for the flamer), and glows even by day | none |
  | Blood | a hit sprays blood out of the target, away from the shooter | none |
  | Zoom in missions | mouse wheel over the map, from 1× to 3×, centred on the cursor | no zoom |
  | Wide screens | in fullscreen (or a window made wider), missions fill the whole width and show more of the map; other screens keep their size, centred. The window can be resized | 640×400 with black bars |
  | Map view limits | the view can be scrolled over the whole map, as people can walk outside the mission area | limited to the mission area |
  | Menu transitions | about 2.8× faster, evenly paced on the display refresh; transition sounds at 80% volume | original timing and volume |
  | Animations (intro, title...) | play at their intended speed | about 40% slower than intended, as in upstream |

  Upstream's frame timer drops the time left over when it fires, and the game loop
  ticks every 30 ms. So an animation meant to run at 66 ms per frame actually ran at
  about 92 ms, with uneven spacing. In hardware mode, animations keep that left-over
  time, and the game loop runs at the display refresh while an animation is playing.
  Gameplay keeps its original 30 ms tick in both modes.
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
- **Colour and logo picker**: the company setup screen shows all the colours and
  logos in a grid, so you can click one directly instead of cycling with the arrows.
- **WASD scrolls the map** in missions, like the arrow keys. Held keys are read every
  frame, so you can scroll diagonally by holding two keys. Ctrl+D is still the
  self-destruct.
- **Move marker**: when you order agents to move (on the map or the minimap), a green
  diamond shows the destination on the floor for a moment.
- **Hold B in a mission to see inside buildings**: the walls and roofs at and above the
  level of the selected agent become see-through, a green outline shows where people
  can walk at that level, and clicks go to that level instead of the roof.
- **Double press 1–4** in a mission to centre the view on that agent (the second
  press must come within 0.4 s).
- **Escape menu in missions**: Escape pauses the game and opens a menu (Resume, Time
  of day, Fullscreen, Lighting, Light editor, Abort mission) instead of aborting the mission
  straight away. Press Escape again to resume. *Lighting* turns the shadows and
  lights on or off (hardware mode, saved as `lighting` in `user.conf`).
- **Light editor** (hardware mode): *Light editor* in the Escape menu keeps the game
  paused and lets you fix how each map tile is lit. Hover the map to see the tile
  under the mouse (white outline), click to select it (every copy on screen gets a
  yellow outline), then left/right click the panel values:
  - *Glow*: None (the default: tiles don't shine in the dark), Bright (the brightest
    pixels shine), Full (the whole tile keeps its brightness, like a pool of light
    painted on the ground).
  - *Light*: Auto (tiles with a lamp light up what's around them), None, On.
  - *Radius*: Auto or 20–160 pixels. *Colour*: Auto (from the tile) or a preset.
  Changes show straight away. *Save* writes `data/ref/tile_lights.ini`, which is in
  the repository, so the settings can be committed. WASD and the mouse wheel still
  move and zoom the view. Escape or *Close* goes back to the game.
- **Alt+Enter** switches between fullscreen and window, on any screen. The choice
  is saved (`fullscreen` in `user.conf`).
- **Time of day override**: in the Escape menu, click *Time of day* to cycle through
  the choices (right-click goes backwards). The change applies straight away and is
  saved for later missions.
  - Hardware mode: *Mission* (the mission's own look), *Day*, *Dusk*, *Night* and
    *Dawn*. They are colour grades applied to the map on the GPU, so they're not
    limited by the palette, and the interface keeps its colours. Saved as
    `time_of_day` in `user.conf` (0 = the mission's own look).
  - Software mode: the original game's five mission palettes (Day Amber, Night, Day
    Green, Dusk Red, Dusk Sand). Swapping the palette also changes the colours of
    the interface, as in the original. Saved as `mission_palette` in `user.conf`.
- **Agent selection keys**: 1–4 select an agent on the equipment screen too (not only
  during missions), and the numeric keypad's 0–4 work like the number keys.
- **Shots from mid-body**: every weapon except the flamer fired from the shooter's
  feet, so the line of fire started at floor level. The floor itself could block it,
  and shots sometimes landed under the agent's feet. Shots, the range limit, and the
  line-of-fire checks (AI sight, "target in range", following a target) now all
  start at half the shooter's height, as the flamer and the aim point already did.
- **No friendly fire at close range**: a friend standing within 2 tiles of the shooter
  doesn't block or take the shot, so agents moving as a group can fire past each
  other. A friend further away can still be hit, and explosions still hurt everyone
  in range.
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
     which otherwise only plays on the first run), **Software mode** (the original
     behaviour, for comparison), **Play mission 1** (starts mission 1 directly, without the briefing and equipment screens), **Jump to mission** (asks for a mission number),
     or **No sound**
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

Development options:

- `-m <n> --play` starts mission `n` directly with the default squad, without the
  briefing and equipment screens.
- `-m <n> --play --export-sprites <file.png>` saves the sprites of mission `n`, read
  from the original files and drawn with the mission palette: the blood pool and the
  death animations of agents first, then the whole sprite sheet. The **Tools: Export
  sprite sheet** launch configuration saves mission 1's in `build/sprites_mission1.png`.
- `--screenshot <file.png>` saves the screen after `--screenshot-delay` ms (default
  3000), then quits. This works headless, so you can compare hardware and software
  modes, for example:

  ```
  freesynd.exe -i dev -u dev/user -n -m 1 --play --screenshot hw.png
  freesynd.exe -i dev -u dev/user -n -m 1 --play --screenshot sw.png --software
  ```

## License

FreeSynd is released under the GNU General Public License, version 3.
See [COPYING](COPYING).
