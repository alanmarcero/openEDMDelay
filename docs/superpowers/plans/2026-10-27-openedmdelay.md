# openEDMDelay implementation plan

Intent: ship the requested native beat-synced audio plugin and GitHub repository. Execute inline, then obtain one independent code review.

## Review focus

- A quarter-note impulse at 120 BPM must first echo at sample 24000 at 48 kHz.
- Dry/wet zero and bypass must preserve the dry signal, regardless of wet effects.
- Missing host BPM must use the visible internal BPM and never claim host sync.
- Automation, extreme feedback and transport changes must remain finite and bounded.
- Session recall must preserve tap positions, slopes, patterns and all effect parameters.

## Tasks

1. Core (`Source/Core/DelayEngine.h/.cpp`, `Tests/DelayTests.cpp`, `CMakeLists.txt`). Define Settings/Tap/Transport, `prepare(double)`, `reset()`, `process(float*,float*,int,const Settings&,const Transport&)`. Write impulse, routing, filter, decay, effects and reset assertions first. Compile against a pass-through stub, confirm failures, implement DSP, run CTest and sanitizers.
2. Plugin (`Source/Plugin/Parameters.*`, `PluginProcessor.*`, `Presets.*`, `Tests/PluginTests.cpp`). Add APVTS with stable parameter IDs, cached raw pointers, host clock selection, bus layouts, programs and state. Verify 120/90 BPM impulse timing, no-host fallback, state round-trip and program recall through the processor.
3. Editor (`Source/Plugin/PluginEditor.*`, `DelayLookAndFeel.*`, `Theme.h`). Reuse the suite's look-and-feel, build live eight-lane tap grid and complete parameter controls. Embed `assets/tape-echo.png`. Integration tests move controls and compare APVTS values, then render two actual editor snapshots.
4. Delivery (`README.md`, `docs/research.md`, `.github/workflows/build.yml`, `scripts/package_notices.py`). Build Release VST3/AU/Standalone, run pluginval, inspect screenshots, review implementation, fix findings and rerun affected tests. Commit and create public GitHub repository using authenticated gh; push main, verify remote SHA and CI. Include build commands, operation details, precise limitations and actual screenshots in README.

## Commands

```sh
cmake -S . -B build-core -DOED_BUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-core --parallel 4
ctest --test-dir build-core --output-on-failure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 6
ctest --test-dir build --output-on-failure
build/openedmdelay_plugin_tests docs/screenshots
```
