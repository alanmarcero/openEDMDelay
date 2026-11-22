# Changelog

## v1.8.2

- **Parameter Locking:** Add per-parameter locking across every plugin parameter, persisted in session state and preserved across preset loads and randomization.

## v1.8.1

- **UI & Suite Fixes:** UI polish, paged lower panels, wheel-safe scroll, engine-accurate dimming, synced preset pickers, feedback-accurate echoes, fixed-aspect scaling, centre-out pan, dBFS output meter, and screen-reader titles.

## v1.8.0

Core DSP engine upgrades across pitch, character, feedback, and dynamics:

- **Shimmer pitch-shift & anti-alias filter:** Octave-up (+12 semitones) shimmer feedback pitch-shifting with a 9 kHz anti-aliasing low-pass filter before feedback injection.
- **Hermite fractional delay interpolation:** 4-point Hermite interpolation for fractional digital taps and stereo Haas offsets.
- **Analog tape head-bump & Ornstein-Uhlenbeck flutter:** 80–110 Hz 2-pole head-bump resonance paired with stochastic Ornstein-Uhlenbeck flutter and wow modulation.
- **Time-scaled BBD filtering:** BBD anti-alias and reconstruction low-pass cutoff scaled inversely with delay time, with 1:2 companding and pre/de-emphasis.
- **Anti-aliased Lo-Fi decimator:** Pre-filtered Lo-Fi decimation to prevent ultrasonic foldover.
- **Stereo-decorrelated unitary Hadamard diffuser:** Energy-preserving unitary Hadamard feedback matrix with distinct prime delay lengths across stereo channels.
- **2x oversampled feedback saturation:** 2x oversampled feedback waveshaping to suppress odd-harmonic aliasing.
- **WSOLA reverse & boundary safety:** WSOLA-based reverse echo splicing with write-boundary clamping to prevent clicks and pre-delay leakage on short taps.
- **Smooth dB-domain ducking:** 5 ms dB-smoothed ducking envelope to eliminate single-sample gain steps and low-frequency ripple.
- **Equal-power rate transitions & BBD varispeed:** Equal-power crossfading on rate changes and continuous varispeed Doppler slewing in BBD mode.
