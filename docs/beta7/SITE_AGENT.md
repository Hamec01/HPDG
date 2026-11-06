# HPDG Beta 7 - website agent handoff

INTERNAL: publication instructions. Do not publish this file or VERIFICATION.md as buyer content. The English PDF, release notes and cleaned email are the customer-facing materials.

## Contents

- HPDG_VST3_Setup_0.1.0-beta.7.exe: HPDG VST3 installer for Windows x64.
- Installer SHA-256 and release.json: release metadata.
- HPDG_Beta7_Guide_EN.pdf: 10 pages covering changes, ATLClassic/DarkTrap, simple formulas, practical workflows, updating and the owner's thank-you message.
- BETA-RELEASE.txt: English release notes and history.
- EMAIL_EN.txt: buyer notification; remove its internal agent note before sending.
- CHANGELOG_EN.md: text version of the guide.
- VERIFICATION.md: checks and outstanding release validation.
- SHA256SUMS.txt: checksums for the other package files.

## Publication sequence

1. Verify the installer checksum and read VERIFICATION.md. Never describe an unfinished check as passed.
2. Upload the installer privately to Contabo, bucket hamloprod-private, using objectKey from release.json.
3. Publish the English PDF for buyers. Update the page from Beta 6 to Beta 7 and from 913 to 919 audio samples. Product: HPDG VST3, Windows 10/11 x64. Do not add removed skin announcements, cross-product announcements, source-code links or internal development details to the page, PDF or buyer email.
4. Apply version/title/notes/page/objectKey/sha256/size from release.json. Make Beta 7 current after the file is available. Keep Beta 6 for rollback.
5. Verify GET /api/hpdg/latest returns version 0.1.0-beta.7 and the new notes, without private storage metadata or secrets. Allow for up to 300 seconds of CDN caching.
6. With a valid test key, verify /api/hpdg/download: signed URL, version, size and checksum. Reject invalid keys. Check the downloaded file's hash.
7. Exercise the real Beta 6 -> Beta 7 Update flow. Download and verify, launch the installer, close the DAW before replacement, install, restart and confirm Beta 7 in the header. Also check manual download and audition the release in the DAW.
8. Send a test email to the owner/test address first. After confirming receipt, send once to active buyers, excluding revoked keys. Prevent duplicates using release-version and campaign identifiers. Insert any key reminder individually on the server.
9. Distinguish accepted/sent from delivered/bounced. Check provider events or actual receipt. Record campaign ID and recipient/result counts. Retry only for recipients who have not already received a send.

Inspect the actual site implementation before using an existing admin button; confirm it performs the required publication and notification steps.

## Short release notes

Distinct ATLClassic and DarkTrap rules; HPDG 808 engine in Sub808; six new ATL/Dark voices (919 samples); Trap Rolls and triplet grid; Basic Pitch key analysis; fixes for 808 crackle and UI-related audio dropouts.
