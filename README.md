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

On iPhones and simulators without a hinge the app shows a fold slider instead. Xcode 27.1's Device Hub has no hinge control, so if the iPhone Duo simulator doesn't show the slider, turn on Settings › Debug › Simulate Hinge (Debug builds). Simulators have no cameras, so the Camera picture needs a real phone.

Core tests run anywhere with a C++20 compiler, no Xcode needed:

```sh
make -C core test
```

## How it works

Two mirrors at angle 180/k degrees make a pattern with 2k images. That pattern only closes without a seam when k is a whole number, and two-operator FM with ratio k:1 is only harmonic when k is a whole number. So the picture and the sound lock at the same moments, and both drift out of tune together in between.

From about 85° iOS counts the phone as closed and moves the app to the cover screen, so k runs from 1 to 8 between flat and 95°, all on the inner screen. Bands are equal width, about 11 degrees each, a little over twice Apple's stated 5 degree accuracy.

| k | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| Hinge (band center) | 180° | 169° | 157° | 146° | 135° | 123° | 112° | 101° |

Detents: a new k commits only after the hinge passes the band edge by about 2 degrees and holds for 0.2 s, then the symmetry and the FM ratio glide there together over about 0.7 s. Quick sweeps commit nothing along the way. Small nudges inside a band turn the glass and brighten the tone without changing the harmony.

Postures:

- Partially open: live control.
- Closed: keeps the last state the phone rested on and keeps playing, with the visuals on the cover screen.
- Fully open: k = 1 (a single mirror) on the inner screen, audio continues.

Controls sit in the iPhone Duo's vertical bar, under the status bar: Hold and Mute at the top, Settings at the bottom. Where the system uses horizontal bars, all three go top trailing. Tap the picture to show them; they fade after 3 s, or stay while VoiceOver is on.

- Hold freezes the symmetry and the harmony so the phone can be put down or regripped. The button shows the current k. On release the hinge takes over again and both glide to its angle.
- Mute fades the drone out and releases the audio session, the same as leaving the app.

### Picture

Settings › Picture chooses what sits behind the mirrors: drifting stained glass (the default) or the camera. The picture fades between the two whenever the camera starts, stops or is interrupted. At k = 1 the right half of the screen shows the right half of the camera's view and the left half mirrors it; at higher k a narrower wedge from the middle of the view is folded round.

The camera is chosen by the way it faces, Faces Away or Faces You, not by whether it's a front or back camera. On iPhone Duo opening or closing the phone moves the app to the other display, so a camera that faced the scene now faces you. An `AVCaptureDeviceDirectionCoordinator` reports which cameras face which way and Dihedra switches when that changes. A rotation coordinator reports how far to turn the image to keep it level, and the shader does the turn, so the capture system doesn't have to rotate every frame. The mirrors multiply every shake of the hand, so the camera's stabilization is always on: the low latency kind where the camera has it, otherwise preview stabilization. Neither adds a noticeable delay, though both crop the view a little. Camera images never leave the phone and nothing is recorded.

### Threads

```
main thread        onHingeChange -> DHCore -> HingeMapper (One Euro filter, detents)
                                                  | relaxed atomics
audio thread       AVAudioSourceNode block -> dihedra::Engine (12-sample blocks, 48 kHz)
render (main)      MTKView draw -> DHCore.frame(at:) -> Shaders.metal
camera queues      AVCaptureSession -> CVMetalTextureCache -> latest frame (mutex) -> render
```

Audio and visuals each run the same critically damped spring toward the same targets, so they glide in step. The render block captures a raw C++ pointer and runs only C++: no Swift, Objective-C, ARC, locks or allocation. The core tests check the no-allocation part.

### Layout

```
App/
  DihedraApp.swift, AppModel.swift
  UI/        RootView (hinge, scene phase, toolbar), SettingsView, FoldFallbackControl
  Render/    KaleidoView (MTKView), Renderer, Shaders.metal, DHShaderTypes.h
  Audio/     AudioController (session, engine, interruptions), SoundMode
  Camera/    CameraFeed (camera by direction, capture into Metal textures), PictureSource
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

## Phase 2 checklist on device

- Camera: the permission prompt appears the first time Camera is chosen, and the picture fades from the glass to the camera and back.
- At k = 1 the camera's view is level in portrait and landscape, on the inner and the cover screen.
- Faces Away and Faces You pick the right camera when partly open, flat and closed, and switch when the phone opens or closes.
- No audio clicks, dropped frames or heat from running the camera alongside the drone.

## Roadmap

1. Hinge to drone and a basic shader
2. Full kaleidoscope shader, camera source (this)
3. Two-operator FM engine from pichenettes/eurorack, k as the ratio
4. FFT bands into the shader
5. Resonator and granular layers

## Contributing

Rules for coding agents (and people) are in [AGENTS.md](AGENTS.md).

## License

The code is MIT licensed (see [LICENSE](LICENSE)). The name Dihedra and the app icon are not part of that license, so a published fork needs its own name and icon.

Phase 3 onward will vendor DSP code from [pichenettes/eurorack](https://github.com/pichenettes/eurorack), using only the MIT-licensed STM32 projects (the AVR projects are GPL). The MIT license and Emilie Gillet's copyright will ship in the app's acknowledgements. The product never uses the "Mutable Instruments" name or module names.
