# openEDMDelay design

Create a native audio effect in the openEDM family: beat-only multitap delay, a polished matching editor, original generated artwork, and a public alanmarcero/openEDMDelay repository with actual editor screenshots.

## Architecture

C++20 with JUCE 8.0.9, matching openEDMFilter. A framework-independent stereo DSP core owns preallocated delay and wet-effect rings. The JUCE adapter supplies host tempo/PPQ, automatable parameters, programs, state, bypass, and mono/stereo buses. Native JUCE controls draw over charcoal panels with cyan/amber details and generated tape artwork. Build VST3, AU on macOS, and Standalone; use conditional formats on other platforms.

Compared approaches: independent feedback per tap consumes more memory and complicates feedback stability; a single stereo ring with eight output heads and feedback at the last active head gives clear phrase repetition and bounded gain. Use the latter. A web editor would add a browser dependency and diverge from the existing native plugins; use JUCE.

## Signal path

Input + phrase feedback -> character -> high-pass -> low-pass -> stereo delay ring -> eight gain/pan heads -> wet glitch -> wet duck -> width -> dry/wet -> output gain. Feedback is taken from the last active head before wet effects. Stereo preserves input channels, Ping Pong injects mono and crossfeeds every phrase, Mono collapses the wet branch. Ping Pong alternates tap sides. Tap pan applies to Stereo.

Digital, Tape, BBD and Lo-fi characters. Tape has saturation and beat-related wow/flutter, and glides when timing changes; digital/BBD/lo-fi crossfade old and new delay heads. Delay positions are 1–16 grid steps, with straight, dotted and triplet grid lengths. Feedback repeats the phrase; decay progressively attenuates later taps. HP/LP slopes: Off, 6, 12, 24, 48 dB/oct. Wet duck: beat envelope or input follower. Glitch: Off, Gate, Stutter, Reverse, with beat subdivisions and amount.

Host BPM drives the clock whenever available in 20–400 BPM. If unavailable, an explicit internal BPM (120 default) drives exactly the same musical timing; the editor indicates Internal. No millisecond delay mode. Host PPQ drives beat effects; internal phase advances when PPQ is missing or transport stopped. Delay ring supports the maximum 32-beat phrase at 20 BPM. Allocate only during prepare. Smooth continuous controls and delay transitions. Clear history on reset and bypass; avoid stale tails on reactivation. Invalid state is ignored. Dry path has zero added latency.

## UI

Header: brand, sync status, factory presets, bypass. Main panel: interactive 16-step timeline with eight tap lanes; click/drag to place taps and set level. Right: original tape mechanism artwork and character/routing selectors. Tap strip: individual enable, step, level and pan controls. Bottom: Time/Feedback/Decay, Filters with independent slopes, Duck/Glitch, Mix/Width/Output. Tooltips describe behavior. Parameters remain host-automatable and session state restores every tap.

## Verification and delivery

Impulse timing at several BPM/sample rates; stereo/ping-pong/mono; normalized multitap gain, decay and feedback; filter frequency response and slopes; character differences; duck/glitch wet-only behavior; reset/bypass; finite output under extreme changes; block-size independence. JUCE integration tests exercise real controls, state, host transport and presets and capture component snapshots from the actual editor. Build native formats, validate VST3 with pluginval and AU with auval when available. Publish source, research notes, screenshots, build instructions and CI. State platform verification precisely.
