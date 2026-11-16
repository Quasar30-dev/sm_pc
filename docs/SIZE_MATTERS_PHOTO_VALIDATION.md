# Photo mode and wrench pass: validation

8 September 2026, Windows x64, USA PS2 ISO. Final native executable SHA256:
`A4902B385ED12EE21B2A2D9059F08E43D33881CE92A08045979498D4E53512F6`.
Theia was rebuilt with the shared skin-reader corrections; executable SHA256:
`FC02082D938ABD2B52AC9CF6157B22A57F723B1DABE803F92C0BAE7ACFCF1D91`.

## Checks

| Check | Result | Evidence under `mods/sm_native/` |
|---|---|---|
| Original wrench table, deformation, independent drive, throw phases/terrain, hyper phases | PASS | `photo_wrench_probe_03.log` |
| Throw hits once per leg; hyper impact damages once; right-hand launch; authored throw sound | PASS | Same probe, assertions in `tests/sm_photo_wrench_probe.cpp` |
| All 306 catalog entries and every clip; finite geometry | PASS, zero unsupported entries | `photo_wrench_probe_03.log` |
| Independent photo instances; pause, loop, last-frame hold, duplicate/remove, scaled pose bounds | PASS | Same probe |
| Existing gameplay, UVs, mounts, RUN rate and independent attachment clocks | PASS | `photo_gameplay_regression_02.log` |
| Existing vendor/help/triggers/checkpoints, moving fire and full three-hit combo | PASS | `photo_level_loop_regression_02.log` |
| Hidden photo-mode UI, original wrench deformation, 4K capture without UI | PASS, zero DX11 errors | `photo_capture_final_4k.log`, `photo_pose_ui_final.bmp`, `photo_clean_final_4k.bmp` |
| Cross-level Dozer character in Pokitaru, complete catalog visible | PASS, zero DX11 errors | `photo_dozer_library_final.log`, matching BMP |
| Wrench flight and airborne hyper strike rendering | PASS, zero DX11 errors | `wrench_flight_side_final.log`, `hyper_strike_final.log`, matching BMPs |

Paused photo captures `photo_pose_final_60.bmp` and `photo_pose_final_120.bmp`
are byte-identical, with SHA256:
`46C894D6C8C5E2215812B73FA58E59D8AFB2E462197E9766DF96DAB205D33686`.
The clean high-resolution image was written at **3840×2160**, then the renderer
returned to its selected **1280×720** display buffer for UI presentation.
Photo mode continues to consume the host input/scheduling clock; it does not
advance active-level ticks or gameplay. Global clock counts in the logs are
not gameplay updates.

The initial catalog run identified 15 unsupported entries. Those failures were
resolved by the original-renderer findings documented in the photo-mode guide;
`photo_wrench_probe_01.log` is retained as investigation history. The final
result is `_03.log`, with all entries passing.

The final link initially encountered a running user session. Its executable was
renamed to `build/x64/sm_pc.before-photo-final.exe`, preserving that session;
the rebuilt `build/x64/sm_pc.exe` is used by `run_size_matters.bat` on the next
launch. No user process was terminated.

These checks establish native behavior and source-data agreement. Physical
XInput testing, emulator timing comparison, exact retail damage/collision
responses and secondary material/effect equivalence remain outside these results.
