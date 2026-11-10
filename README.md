# Dihedra

A pay-once kaleidoscope and drone for iPhone Duo. The hinge is the instrument: fold the phone and the mirror angle changes, so the symmetry of the picture and the harmony of the sound change together.

Named for the dihedral group, the symmetry of two hinged mirrors.

## Build

Requires Xcode 27.1 (iOS 27.1 SDK) and XcodeGen.

```sh
brew install xcodegen
cp Config/Local.xcconfig.example Config/Local.xcconfig   # add your team ID
xcodegen
open Dihedra.xcodeproj
```

The iPhone Duo simulator in Device Hub can open, close and fold the device. On other simulators and iPhones the app shows a fold slider instead.

Core tests run anywhere with a C++20 compiler, no Xcode needed:

```sh
make -C core test
```

## How it works

Two mirrors at angle 180/k degrees make a pattern with 2k images. That pattern only closes without a seam when k is a whole number, and two-operator FM with ratio k:1 is only harmonic when k is a whole number. So the picture and the sound lock at the same moments, and both drift out of tune together in between.

The hinge range is stretched so k runs from 1 to 8 over the angles where the inner screen is still easy to see. Bands are equal width, about 16 degrees each, roughly three times Apple's stated 5 degree accuracy.

| k | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| Hinge (band center) | 180° | 164° | 148° | 132° | 116° | 100° | 84° | 68° |

Detents: a new k commits only after the hinge passes the band edge by about 3 degrees and holds for 0.2 s, then the symmetry and the FM ratio glide there together over about 0.7 s. Quick sweeps commit nothing along the way. Small nudges inside a band turn the glass and brighten the tone without changing the harmony.

Postures:

- Partially open: live control.
- Closed: keeps the last state the phone rested on and keeps playing, with the visuals on the cover screen.
- Fully open: k = 1 (a single mirror) on the inner screen, audio continues.

Controls sit in the iPhone Duo's vertical bar, under the status bar: Hold and Mute at the top, Settings at the bottom. Where the system uses horizontal bars, all three go top trailing. Tap the picture to show them; they fade after 3 s, or stay while VoiceOver is on.

- Hold freezes the symmetry and the harmony so the phone can be put down or regripped. The button shows the current k. On release the hinge takes over again and both glide to its angle.
- Mute fades the drone out and releases the audio session, the same as leaving the app.

### Threads

```
main thread        onHingeChange -> DHCore -> HingeMapper (One Euro filter, detents)
                                                  | relaxed atomics
audio thread       AVAudioSourceNode block -> dihedra::Engine (12-sample blocks, 48 kHz)
render (main)      MTKView draw -> DHCore.frame(at:) -> Shaders.metal
```

Audio and visuals each run the same critically damped spring toward the same targets, so they glide in step. The render block captures a raw C++ pointer and runs only C++: no Swift, Objective-C, ARC, locks or allocation. The core tests check the no-allocation part.

### Layout

```
App/
  DihedraApp.swift, AppModel.swift
  UI/        RootView (hinge, scene phase, toolbar), SettingsView, FoldFallbackControl
  Render/    KaleidoView (MTKView), Renderer, Shaders.metal, DHShaderTypes.h
  Audio/     AudioController (session, engine, interruptions), SoundMode
  Bridge/    DHCore.h/.mm (Objective-C++ face of the core), bridging header
core/
  include/dihedra/  Tuning, Spring, OneEuroFilter, HingeMapper, HighPass, Engine
  tests/            core_tests.cpp
tools/shader_preview.py   numpy port of the shader for quick look checks
```

## Sound settings

iOS can't respect the Silent switch and keep playing when locked at the same time: the ambient category obeys the switch but stops on lock, and background playback needs the playback category, which ignores the switch. So the setting has three choices: Respect Silent Mode (default), Always Play While Open, and Keep Playing When Locked.

## Phase 1 checklist on device

- Does settling into a detent feel good? Tune `kDwellSeconds`, `kHysteresis` and `kSymmetryOmega` in `core/include/dihedra/Tuning.h`.
- Does `onHingeChange` fire on launch, or only once the hinge moves? The app waits 1.5 s before assuming there's no hinge.
- How often do hinge updates arrive while moving and while still?
- Closing: does the app stay active on the cover screen, and does the latched state hold?
- Audio: no clicks at detent glides, launch or backgrounding, in each sound mode.
- Controls: in the vertical bar on the cover screen and on the inner screen in landscape, and in a horizontal bar on the inner screen in portrait. No clicks when muting or unmuting.

## Roadmap

1. Hinge to drone and a basic shader (this)
2. Full kaleidoscope shader, camera source
3. Two-operator FM engine from pichenettes/eurorack, k as the ratio
4. FFT bands into the shader
5. Resonator and granular layers

## Contributing

Rules for coding agents (and people) are in [AGENTS.md](AGENTS.md).

## License

The code is MIT licensed (see [LICENSE](LICENSE)). The name Dihedra and the app icon are not part of that license, so a published fork needs its own name and icon.

Phase 3 onward will vendor DSP code from [pichenettes/eurorack](https://github.com/pichenettes/eurorack), using only the MIT-licensed STM32 projects (the AVR projects are GPL). The MIT license and Emilie Gillet's copyright will ship in the app's acknowledgements. The product never uses the "Mutable Instruments" name or module names.
