# Delay references

Research date: 2026-11-16. These products informed the feature selection; openEDMDelay uses original DSP and artwork. It does not load Virus patches or claim a bit-accurate emulation.

| Primary source | Relevant behavior | openEDMDelay design |
|---|---|---|
| [Vengeance Essential FX Bundle 1](https://www.vengeance-sound.com/plugins/vengeance-producer-suite-essential-fx-bundle-1) | Delay includes pattern modes, ping-pong and tape effects; parameters are automatable. | Pattern selector, separate routing and character, all controls exposed to the host. |
| [Waves SuperTap manual](https://assets.wavescdn.com/pdf/plugins/supertap-1.pdf) | Six taps, tempo grid, tap gain and stereo position, filtering, modulation and feedback. | Eight tap lanes with editable step, level and pan, plus a shared feedback phrase. |
| [Waves H-Delay manual](https://www.waves.com/1lib/pdf/plugins/h-delay-hybrid-delay.pdf) | Crossfed stereo ping-pong, tape-like pitch changes when time moves, modulation, lo-fi and high/low-pass filtering. | Crossfed phrase repeats, tape glide/wow, lo-fi character and independently selectable filter slopes. |
| [Soundtoys EchoBoy product page](https://www.soundtoys.com/product/echoboy/) and [user guide](https://www.soundtoys.com/wp-content/uploads/EchoBoy-Manual.pdf) | The guide describes feedback-loop or post diffusion with adjustable Size, plus wobble depth, rate, and five shapes: Triangle, Sine, Square, Random Walk, and Random Sample-and-Hold. | A four-stage all-pass network adds progressive feedback-loop diffusion with Amount and Size. Tape Motion retains its depth control and adds a free-running Rate plus the five waveform choices; the random shapes are deterministic. These are original algorithms, not EchoBoy styles or hardware models. |
| [Access Virus reference manual](https://www.virus.info/download/200), available through [Access manuals](https://www.virus.info/support/manuals) | Delay section (PDF page 35) documents Simple, Ping Pong 2:1 through 8:7, and clock-synchronized Pattern 1+1 through 5+5. | Original rhythm templates include 1+1, 2+1, 3+2, 3+4, 3+5, 4+3, 5+3, plus eight-tap sequences. The shared-ring topology is not the Virus topology. |
| [Access OS3 tape echo description](https://www.virus.info/virusti/upgrades) | Tape delay follows tempo; Doppler behavior during tempo changes is a deliberate sound choice. | Tape character glides; other characters crossfade read heads over 25 ms. |

Additional design decisions: beat-only controls throughout; visible internal BPM fallback; ducking and glitch on the wet branch; filters inside the feedback loop. A later version adds an optional sidechain input as a third duck source. Patterns describe useful rhythmic relationships rather than proprietary presets. Tape, BBD and lo-fi are creative approximations, not hardware circuit models.

## Visual source

`assets/tape-echo.png` is original AI-generated artwork made for this plugin. It is embedded in the native editor. The shared typography, charcoal/cyan/amber palette and rotary control rendering follow the user's existing openEDMFilter UI. README screenshots are generated from the actual compiled JUCE editor by the integration test, with a simulated 128 BPM host.
