# Apple Vision Pro port

The decompiled game, built natively for visionOS. Matches stand as a diorama
on your table; the menus and the HUD stay in a window beside it.

For your own device only. The app contains no game data: you supply a disc
image of your own copy of the game (GALE01, v1.02), which the build and the
app read from.

## Building

You need a Mac with Xcode 16 or later (visionOS 2 SDK), CMake 3.28 or later,
Python, and the repository's usual toolchain (see `docs/getting_started.md`).

1. Put your copy of the game in `orig/GALE01` as the matching build expects,
   then build it once. This extracts the fonts from `main.dol` into
   `build/GALE01/include`, which the port compiles in:

   ```sh
   python configure.py && ninja
   ```

2. Export the game's types for the archive loader. This writes
   `build/GALE01/dat/types.bin`, from which configuring the port generates
   the archive schema (`tools/dat-cli/native`):

   ```sh
   cmake --preset dat && cmake --build --preset dat
   ```

3. Configure and open the app. This fetches Aurora at the commit the dev
   shell pins, and applies `port/aurora/*.patch`:

   ```sh
   cmake --preset visionos \
       -DMELEE_BUNDLE_ID=com.yourname.diorama \
       -DMELEE_DEVELOPMENT_TEAM=YOURTEAMID
   open build/visionos/melee.xcodeproj
   ```

   Pick the `Diorama` scheme and your Vision Pro, then run. For the
   simulator, use `--preset visionos-sim`; it has no controllers or
   tables, but shows the window and the immersive space.

4. On first launch, choose your disc image. The app copies it into its own
   storage.

## Controls

The PS VR2 Sense controllers, one in each hand, together make one GameCube
controller:

| Left hand    | GameCube | Right hand   | GameCube |
|--------------|----------|--------------|----------|
| stick        | stick    | stick        | C-stick  |
| L2 (trigger) | L        | R2 (trigger) | R        |
| L1 (grip)    | D-pad*   | R1 (grip)    | Z        |
| □            | X        | ✕            | A        |
| △            | Y        | ○            | B        |
| create       | START    | options      | START    |

\*Hold L1 and the left stick works as the D-pad (taunts, Training Mode).

L and R are analog; the digital click registers at the end of the
trigger's travel. A gamepad (DualSense, Xbox) works the same way, with its
own D-pad. Mapping lives in `Sources/InputMapper.swift`.

In the immersive space, pinch a table to move the stage there. Use the
window's slider to resize it.

## How it fits together

| Where | What |
|---|---|
| `src/port/` | The game's side, built only for the port: boot and frame loop (`port_main.c`), the retrace interrupt and the SDK pieces Aurora lacks (`port_os.c`), archives converted to native types (`port_dat.c`), vertex array lengths (`port_gx.c`), spatial passes (`port_spatial.c`), input (`port_input.c`). `port.h` is the app's interface to all of it. |
| `#ifdef TARGET_PC` in the game | Hooks into the frame loop (`gmscene.c`, `video.c`), the camera (`cobj.c`), the screen clear (`displayfunc.c`), archive loading (`archive.c`, `lbarchive.c`) and vertex arrays (`pobj.c`). The GameCube build is unchanged. |
| `port/aurora/` | Patches to Aurora: presenting to a CAMetalLayer the app owns, and to IOSurfaces (one per eye). |
| `platform/visionos/` | The app: SwiftUI window, CompositorServices immersive space, ARKit table finding, controller mapping. |
| `cmake/Port.cmake`, `cmake/visionos.cmake` | The build. |

Each frame, the game's logic runs once. In spatial mode it then draws its
perspective cameras once per eye, from the eye's pose and with a frustum
fitted to the stage, so the far backdrops fall outside it. Then it draws its
2D cameras (the HUD) into the window. The compositor shows the latest eyes,
reprojected from the pose they were drawn from.

## Status

Written on Windows and not yet built or run. Expect to fix compile errors in
the Swift and Aurora code the first time.

Checked so far: every game and port source passes a 64-bit syntax check, and
the Aurora patch applies to the pinned commit.

Not yet checked:

- **Dawn on visionOS.** Aurora builds Dawn for iOS; `cmake/visionos.cmake`
  presents visionOS as iOS to its build, which may need more work.
- **That the GameCube build still matches.** Every change is meant to be
  neutral under MWCC; run `ninja` to confirm 100%.
- **Whether the archive schema covers every root the game loads.** Missing
  ones log `port: no type for ...`; add them to `config/GALE01/dat.yml`.
- **How the PS VR2 Sense controllers present.** The mapper works out each
  hand from its face buttons. If they show up under other element names,
  adjust `InputMapper.hand(of:)`.
- **CompositorServices details:** the depth range convention and the
  tangent signs (`SpatialRenderer.submit`).

Known gaps:

- **Movies** (THP) don't decode, so they play black (`src/port/port_thp.c`).
- **Fog range adjustment** isn't drawn.
- **Stages' backdrops** that fall inside the stage's sphere still render;
  some stages may need theirs hidden.
- **Memory:** archives the game loads into fresh buffers stay open until
  their buffer is reused.
