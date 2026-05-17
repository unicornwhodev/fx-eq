# Manual QA Checklist

Scope: Debug standalone builds under build/*/*_artefacts/Debug/Standalone.

Smoke-launch status:
- Analyzer: pass
- Creative: pass
- Delay: pass
- Distortion: pass
- Doubler: pass
- Dynamics: pass
- EQ: pass
- Modulation: pass
- Pitchtime: pass
- Reverb: pass
- Stereo: pass

Core pass for every plugin:
- Launch standalone app and confirm window opens without crash.
- Load audio input and confirm output is present.
- Toggle Bypass on/off repeatedly and confirm no stuck state, pop burst, or UI desync.
- Test presets: previous, next, select from list, save if enabled, and A/B recall if present.
- Sweep output to low, nominal, and hot levels.
- Verify all meters and status LEDs react to real signal rather than stale UI state.

Expected manual checks by plugin:

## Analyzer
- Check Freeze on/off: visualization should stop and resume cleanly.
- Check Source In/Out switching: analysis source label and color should track the routed source.
- Check Mix extremes if exposed in preset/state: dry analysis view vs processed emphasis should remain stable.
- Check Active LED only lights when signal is present and analysis is not frozen.

## Creative
- Check Mix at 0% and 100%: dry-only vs full texture effect.
- Check chaos extremes: calm, motion, and warp states should change UI text and feel coherent.
- Check sync on/off while audio is running: label and timing behavior should follow host/free mode correctly.
- Check bypass while tails are active: no stale LED or broken visual animation state.

## Delay
- Check mono/stereo input toggle with a stereo source.
- Check sync/free switching at short and long times, including the displayed effective time.
- Check feedback near minimum, musical range, and extreme range; Clip LED should reflect real output overload risk.
- Check ping-pong/stereo mode and confirm the visualization matches the heard tap spread.

## Distortion
- Check Mix at 0% and 100%.
- Check all modes: Clipper, Bitcrush, Tube.
- Check mono on/off with stereo content.
- Drive hard enough to light the Clip LED and confirm UI state matches audible saturation.

## Doubler
- Check Mix at 0% and 100%.
- Check input mono on/off and stack mono/stereo on/off with headphones.
- Check voices at minimum and maximum, plus spread/detune/drift extremes.
- Confirm Active LED tracks real output activity, not just voice count or toggle state.

## Dynamics
- Check bypass against compressed state at threshold extremes.
- Check Mix at 0% and 100% if exposed in the current build.
- Check mono/stereo input mode.
- Push hot input to verify gain-reduction indicators and meter behavior remain coherent.

## EQ
- Check Mix at 0% and 100%.
- Check mono/stereo input mode.
- Boost multiple bands into headroom protection and confirm SAFE/TRIM state updates visually.
- Sweep Q and all 5 bands, then verify no visual overlap or stale indicator state in the header.

## Modulation
- Check Mix at 0% and 100%.
- Check mono/stereo input mode.
- Check filter type changes and confirm the visualization follows the effective cutoff.
- Check envelope and LFO extremes: ENV OFF, strong ENV, slow LFO, fast LFO; Clip LED should react to real output overload.

## Pitchtime
- Check Mix at 0% and 100%.
- Check mono/stereo input and stack mono/stereo.
- Check voices minimum and maximum, then formant dark/neutral/bright states.
- Verify Active LED and formant button state remain coherent across voice-count changes.

## Reverb
- Check Mix at 0% and 100%.
- Check mono/stereo input mode.
- Check Freeze on/off during live audio and after signal stops; Freeze LED should stay authoritative.
- Drive hot wet settings and confirm WET SAFE trim state updates without broken UI feedback.

## Stereo
- Check Mix at 0% and 100%.
- Check mono/stereo input mode.
- Check width, Haas, and bass-mono style controls at extremes with a correlated stereo source.
- Verify any mono-risk or activity indication follows actual correlation/output behavior.

Pass criteria:
- No crash, hang, or immediate audio dropout.
- UI text, button color, LED state, and visualization all match the audible state.
- Extreme values do not leave stale indicators behind after returning to nominal settings.