# HPDG Beta 7 - final English handoff (2026-11-06)

Later work in this checkout: LuxuryTrap reconstruction is recorded separately in
[LUXURY_TRAP_HANDOFF.md](LUXURY_TRAP_HANDOFF.md). The Beta 7 release handoff below is preserved;
its packaged binary predates the LuxuryTrap changes.

Owner requested that the Beta7 changes also be committed and pushed with the LuxuryTrap work.
The current source library has 998 audio assets (997 WAV + 1 AIF); installer count/release notes
now match it. The existing packaged installer/site ZIP and their verification below predate
LuxuryTrap. Rebuild and reverify those packages before publishing a release with LuxuryTrap.

Owner will pass the package to the website agent. No publication or buyer-email sending was performed here.

Latest requirements: English only; buyer materials cover HPDG itself; no removed skin announcement, cross-product announcements, source-code links, commit IDs or internal report/file references in customer content.

Deliverables:
- output/pdf/HPDG_Beta7_Guide_EN.pdf: 10 visually checked A4 pages, ATLClassic/DarkTrap, rhythm diagrams, simple verified formulas, 808/sample workflows, update instructions and owner's thanks/feedback invitation.
- Releases/HPDG_Beta7_Site_Package_EN.zip: explicit English-only payload; SITE_AGENT.md and VERIFICATION.md are INTERNAL and must not be published as buyer content.
- Releases/HPDG_Beta7_Site_Package.zip: refreshed compatibility copy of the final English archive; supersedes the earlier mixed-language package.
- Installer: Releases/HPDG_VST3_Setup_0.1.0-beta.7.exe; 122160358 bytes; SHA256 40aac7abeb7b3d4f397b24ce0eda80f94e220c0cac95bc62b463edfeb1c35b87.

Verification:
- Fresh VST3 Release build; beta.7 tag in binary, EXE ProductVersion 0.1.0 Beta 7 / FileVersion 0.1.0.7.
- 919 audio samples (six ATL/Dark additions); 1002 total library files verified by SHA256, no stale files.
- 10/10 test suites passed by latest result. Primary Lane failure was due to working directory without Samples; CMake now gives LaneBoundaryTests the proper working directory. Thresholds and DSP unchanged. Logs/beta7-tests-primary.xml preserves the initial failure; Logs/beta7-tests-final.xml records the successful correction. All 10 test targets were rebuilt.
- Installer was repackaged after English copy corrections, then isolated install/hash/document/DLL load/uninstall checks passed again. Logs/beta7-package-en.log, Logs/beta7-install-test-en.log; build/installer-smoke-f357085e6ad54528bd105cda232fe210.
- Final English PDF: all pages rendered and visually inspected. Packaging checks English-only customer text, excluded content, successful test results, payload checksums and ZIP integrity.

Reproduce PDF: bundled Python tools/build_beta7_guide.py (content in docs/beta7/guide_en.json).
Reproduce transfer archive: bundled Python tools/package_beta7.py.
No algorithm changes in release preparation. No commits/pushes or model/effort switch. No active build/test jobs at completion. Owned changes include release CMake/FAQ/installer count/readme/release notes, docs/beta7, generation/package scripts, this handoff and output PDF; test XMLs are owned untracked proof files.

Next: follow SITE_AGENT.md inside the archive. Publish private EXE and English guide, apply release.json, validate production Beta 6 -> Beta 7 Update and manual download, open/audition in DAW, confirm test email receipt, then send one buyer notification per release. Production flow, DAW audition and email delivery remain unverified here.
Recommended effort: medium for publication/known checks; Astra xhigh if a substantive DSP/compatibility issue needs diagnosis. Owner changes settings if needed.
