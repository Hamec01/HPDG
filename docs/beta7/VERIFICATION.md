# Beta 7 - internal verification record

INTERNAL: do not publish this record to buyers.
Date: 2026-11-06.

## Build and installation

- Fresh HPDG VST3 Release build succeeded. Version: 0.1.0-beta.7; EXE resource FileVersion: 0.1.0.7.
- Six added ATL/Dark WAVs bring the factory audio count from 913 to 919. The bundle contains 1002 library files including metadata. Hash checks found no missing, outdated or extra files.
- Initial packaging rejected the previous 913-file expectation. After updating the count and release notes, packaging passed.
- Isolated installer validation passed: all 1002 library hashes, obsolete-file cleanup, documentation, DLL factory loading and uninstall. The system-installed plugin was not replaced.
- After the English release-note corrections, packaging and isolated installation were repeated successfully. Logs: Logs/beta7-package-en.log and Logs/beta7-install-test-en.log; detailed install logs: build/installer-smoke-f357085e6ad54528bd105cda232fe210.
- The English revision changes documentation only. The installer is repackaged to include corrected customer-facing release notes; see release.json and the checksum sidecar for its current exact size and SHA-256.

## Tests

- All 10 test targets rebuilt in Release, exit 0.
- CoreTests, Sub808EngineCheck and HamloLinkTests passed in the primary run.
- LaneBoundaryTests initially failed because its working directory did not contain Samples. Its CTest working directory was corrected to the development sample root, with assertions and thresholds unchanged.
- LaneBoundaryTests and the six remaining suites then passed 7/7. Latest result per suite: 10/10 PASS. The initial failure remains recorded in the primary test log.
- The compiled VST3 contains the beta.7 version tag. No DSP or generation algorithm was changed during release preparation.

## English PDF and package

- Ten A4 pages with embedded fonts and searchable/copyable English text.
- All final pages are rendered with Poppler and visually checked before delivery. Authoring also checks for text overflowing the footer.
- Rhythm diagrams are teaching examples. Timing and pitch formulas are checked; extra-kick and selection-scoring explanations match generator behaviour.
- The English package uses an explicit allowlist, excluding earlier Russian documents. Customer-facing text is checked for the removed announcements and internal development references.
- Final packaging validates PDF text, the ten successful test-suite results and file hashes. The ZIP is checked for CRC errors and consistency with release.json.

## Still required before the buyer broadcast

- Publish the actual installer and release metadata.
- Verify the production Beta 6 -> Beta 7 download/install path and manual download.
- Open and manually audition this release in the DAW.
- Confirm receipt/provider delivery of a test email before sending to buyers.

The production version was Beta 6 at the audit. Publication and email sending have not been performed here. Existing audio-lock limitations remain; glitch-free playback under every host/load condition is not promised.
