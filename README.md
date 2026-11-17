# openEDMDelay v1.2.1

Build beat-synced echoes from eight taps. Use it for rhythmic repeats, stereo movement, and glitch transitions.

[Download the latest release](https://github.com/alanmarcero/openEDMDelay/releases/latest) · [OpenVST Manager](https://github.com/alanmarcero/alan-m-OpenVSTManager/releases/latest)

![openEDMDelay editor](docs/screenshots/openEDMDelay.png)

*Native editor capture.*

## Features

- **Eight taps:** Enable each tap, set its grid position and level, and place it in the stereo field. Drag the tap display to change time, level, or pan. Right-click a tap to mute it.
- **Rhythms and presets:** Choose from 12 rhythm templates and 36 factory programs for trance, techno, house, drum and bass, hardstyle, garage, and rave.
- **Beat grid:** Choose straight, dotted, or triplet divisions from 1/32 to a dotted whole note. Each tap can use 1–16 steps, for phrases up to 96 beats.
- **Stereo and character:** Choose Stereo, Ping Pong, or Mono routing. Ping Pong alternates repeats between left and right. Stereo follows each tap's pan. Choose Digital, Tape, BBD, or Lo-fi for the feedback sound. Tape Motion sets wobble depth. Wobble Rate ranges from 0.05 to 8 Hz with five waveforms.
- **Per-tap shaping:** Set Chance from 0–100% and add 1–8 ratchets to each tap. Add optional 12 dB per octave high-pass or low-pass filters, color, and drive.
- **Groove controls:** Adjust Swing or choose Manual, Shuffle, or Broken groove. Pattern Morph moves taps toward their editable target steps.
- **Feedback and diffusion:** Feedback repeats the phrase. Decay reduces later repeats. High-pass and low-pass feedback filters offer Off, 6, 12, 24, or 48 dB per octave. Diffusion softens repeats with Amount and Size controls from 2 to 30 ms.
- **Hold and beat effects:** Hold recirculates the current phrase without new input. Duck the wet signal from the beat or input level. Duck Rate controls beat repetition or follower recovery.
- **Wet effects:** Add Gate, Stutter, or Reverse effects with their own beat rate and mix.
- **Output:** Set dry/wet Mix, stereo Width, and output gain. Use bypass. Controls support host automation and session recall. The dry path adds no latency.

Delay grids and beat effects follow host tempo from 20 to 400 BPM when available. The internal clock uses 120 BPM by default. Beat effects use the host clock during playback and the internal clock when stopped. Input ducking listens to the main input. There is no separate sidechain bus.

![Broken Transmission program](docs/screenshots/openEDMDelay-glitch.png)

![Tap, groove, filter, hold, and diffusion controls](docs/screenshots/openEDMDelay-performance.png)

## Install and compatibility

Install the VST3 or AU with [OpenVST Manager](https://github.com/alanmarcero/alan-m-OpenVSTManager/releases/latest). Download the standalone app or manual packages from the [latest release](https://github.com/alanmarcero/openEDMDelay/releases/latest).

| Platform | Formats |
| --- | --- |
| macOS, Apple Silicon or Intel | VST3, AU, Standalone |
| Windows, x64 | VST3, Standalone |
| Linux, x64 | VST3, Standalone |

The plugin supports mono and stereo audio. Rescan your DAW after installing. Set Mix to 100% when you use the delay on an auxiliary return.

## License

openEDMDelay uses the AGPL-3.0 license. See [LICENSE](LICENSE) and [third-party notices](THIRD_PARTY.md).
