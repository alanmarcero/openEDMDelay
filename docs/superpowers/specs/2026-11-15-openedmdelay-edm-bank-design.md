# openEDMDelay EDM bank and synchronized grid

## Goal

Expand the factory bank with EDM-focused programs and make the delay, duck, and glitch grids useful across short subdivisions and long phrase echoes. Every rate remains musically timed from the host BPM/PPQ when supplied, with the existing internal clock as the no-host fallback.

## Design

- Expand the shared rate list from ten to sixteen divisions: add dotted 1/32 and 1/16, triplet 1/2, dotted 1/2, whole note, and dotted whole note. Append the new choices so existing saved parameter values retain their meaning.
- Add twelve factory programs across trance, techno, house, drum and bass, hardstyle, garage, and rave styles. Use existing taps, character, routing, filters, ducking, drive, motion, and glitch controls to show the full instrument range.
- Preserve the existing host clock selection and parameter IDs. The expanded choices feed the existing delay, beat-duck, and glitch timing paths.
- Keep the eight-tap layout and the engine's preallocated history model. The longest new rate is six beats; the existing delay ring supports the maximum 96-beat phrase and the glitch history supports the reverse window.

## Verification

- Test all sixteen divisions against host-tempo impulse positions in the DSP core.
- Load all 36 programs through the real processor, check active tap patterns and valid grid choices, and confirm host sync remains active when the host supplies BPM.
- Build all native formats, run core and plugin integration tests, package, then follow tagged GitHub Actions through release publication.
