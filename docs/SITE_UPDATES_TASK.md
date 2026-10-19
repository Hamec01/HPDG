# Task for the hamloprod.org agent: HPDG license keys and in-plugin updates

Date: 2026-10-19. Repository: `hamloprod-web` on the HamloProd server (Next.js, runs on Vercel;
database `hamloprod-postgres` via Prisma; private files in Contabo S3 bucket `hamloprod-private`).
Owner: HamloProd. Plugin side is done in the HPDG repository (`Source/Services/UpdateCheckService.*`,
`Source/UI/UpdateButtonComponent.*`); this file is the contract the site must implement.

## Decisions (owner, 2026-10-19)

- The HPDG installer goes **only to buyers**. No public GitHub release repository
  (`Hamec01/HPDG_beta-releases` is not created and not used).
- Each purchase gets a **license key**; the buyer types it into the plugin once, and the plugin
  downloads, verifies (SHA-256) and starts the installer itself.
- Every new version: the owner replaces the installer in storage, updates the release record and
  an email goes to all buyers.
- No secret lives in the plugin. All secrets live in Vercel environment variables.

## What to build

### 1. Release record

One record describing the current release: a row in Postgres (Prisma model, editable in the admin
without a deploy) or `config/hpdg-release.json` in the site repo:

```json
{
  "version": "0.1.0-beta.4",
  "title": "HPDG 0.1.0 Beta 4",
  "notes": "Short change list, plain text, up to ~600 characters (shown as the button tooltip).",
  "page": "https://hamloprod.org/en/vst",
  "objectKey": "hpdg/HPDG_VST3_Setup_0.1.0-beta.4.exe",
  "sha256": "<64 lowercase hex>",
  "size": 113853262
}
```

The owner computes the hash with
`(Get-FileHash HPDG_VST3_Setup_0.1.0-beta.4.exe -Algorithm SHA256).Hash.ToLower()`.

### 2. Installer storage

Contabo S3, bucket `hamloprod-private`, folder `hpdg/` — the same presigned-URL code that already
serves paid WAV / ZIP files. The file is ~115 MB, so it must **never be streamed through a Vercel
function** (4.5 MB response limit): `/download` returns a presigned link (10 minutes) and the plugin
downloads from Contabo directly.

The plugin accepts download links only on `https://hamloprod.org`, `https://www.hamloprod.org` and
`https://usc1.contabostorage.com/hamloprod-private/hpdg/<file>` (path-style, one file directly in
`hpdg/`; no other bucket or folder, no `..` or encoded `%2e` / `%2f` in the path; the query string is
free). Anything else is refused. Redirects (a hamloprod.org URL that 302s to Contabo) are fine.

### 3. `GET /api/hpdg/latest` (public)

200, `Content-Type: application/json`, from the release record, **without** `objectKey`:

```json
{ "version": "0.1.0-beta.4", "title": "HPDG 0.1.0 Beta 4", "notes": "...", "page": "https://hamloprod.org/en/vst" }
```

`version` is `MAJOR.MINOR.PATCH[-label.N]` (`0.1.0-beta.4`, `0.1.0-rc.1`, `1.0.0`). `page` must be
on hamloprod.org (the plugin replaces any other host with https://hamloprod.org/en/vst).
Cache: `s-maxage=300`. The plugin calls it once per DAW session and on each click of Update.

### 4. `POST /api/hpdg/download`

Request (`Content-Type: application/json`):

```json
{ "licenseKey": "HPDG-7K3M-2Q9X-ABCD-EF01", "currentVersion": "0.1.0-beta.3", "platform": "windows-x64" }
```

Responses — the plugin reacts to the **status code**:

| Status | Body | Plugin shows |
|---|---|---|
| 200 | `{ "url": "...", "sha256": "<64 hex>", "version": "0.1.0-beta.4", "size": 113853262 }` | downloads, checks SHA-256, offers to run the installer |
| 401 / 403 | `{ "error": "invalid_key" }` (unknown, refunded or revoked key) | asks for the key again |
| 429 | `{ "error": "rate_limited" }` | "download limit for today" |
| 404 | `{ "error": "no_release" }` | "not available yet" |
| other | — | "hamloprod.org did not answer" |

Rules:
- Normalize the key the way the plugin does (uppercase, `HPDG-` + 4 groups of 4 Crockford base32
  chars) before the lookup; compare in constant time.
- Limit: 5 successful tickets per key per 24 h, plus a per-IP limit against key guessing
  (e.g. 20 failed attempts per IP per hour → 429).
- Log each ticket (key id, time, version, IP country) for support.

### 5. License keys

- Format `HPDG-XXXX-XXXX-XXXX-XXXX`, alphabet `0123456789ABCDEFGHJKMNPQRSTVWXYZ` (Crockford base32,
  no I, L, O, U), 16 random characters = 80 bits from `crypto.randomBytes`.
- Storage: a new Prisma model in `hamloprod-postgres`. Record:
  `{ key, email, stripeCustomerId, stripeSessionId, tier, createdAt, revoked, downloads[] }`.
- Stripe webhook `POST /api/stripe/webhook`, event `checkout.session.completed` (and
  `charge.refunded` → `revoked: true`): verify the signature with `STRIPE_WEBHOOK_SECRET`,
  be idempotent by session id, create the key, email it via Resend from
  `HamloProd <hello@hamloprod.org>` together with the installer link for the first install
  (a link to a page that asks for the key and calls `/download`, or a one-time ticket link).
- Existing buyers of Beta 1–3: a one-off script (`scripts/backfill-hpdg-keys.ts`) that lists paid
  Stripe checkout sessions for the HPDG product, creates a key for each email that has none, and
  emails it. Dry-run flag first; the owner runs it once.

### 6. "New version" email

A protected admin route or script (`ADMIN_TOKEN` in env) that emails every non-revoked key holder:
new version, change notes, "open HPDG in your DAW and press **Update** in the header" and their key
as a reminder. Use Resend batch sending; respect the free-plan limits.

### Environment variables (Vercel only, never in the plugin)

`STRIPE_SECRET_KEY`, `STRIPE_WEBHOOK_SECRET`, `RESEND_API_KEY` (or SMTP), the existing S3 and
database credentials, `ADMIN_TOKEN`.

## Order (agreed with the site agent, 2026-10-19)

Not available yet on the site: a Stripe webhook (sales go through a plain Stripe payment link),
any mail service (Resend is not connected to the site), keys for Beta 1–3 buyers. So:

1. Key table (Prisma).
2. `/api/hpdg/latest` and `/api/hpdg/download` with the key check and the daily limit.
3. The installer uploaded to `hamloprod-private/hpdg/`.
4. Manual key issue in the admin (by email, or from the Stripe payment list).

That is enough for in-plugin updates. Automatic keys after payment (section 5 webhook) and the
emails (section 6) follow once Stripe API / webhook secrets and Resend or SMTP are provided.

## Owner's routine for a new version

1. Build the installer in the HPDG repo (`Installer/Build-Beta.ps1`), compute SHA-256.
2. Upload it to `hamloprod-private/hpdg/`, replacing the previous one.
3. Update the release record (version, title, notes, objectKey, sha256, size).
4. Run the "new version" email.

## Testing against a local server

The plugin reads `HPDG_UPDATE_API` from the environment; it accepts only `http://localhost...` or
`http://127.0.0.1...` (e.g. `http://localhost:3000/api/hpdg`). Set it, then start the DAW from the
same shell. With the override, download links on localhost are allowed too.

## Done when

- `curl https://hamloprod.org/api/hpdg/latest` returns the record above.
- A test purchase (Stripe test mode) creates a key and the email arrives.
- `/download` with that key returns a working link whose file matches `sha256`; a wrong key → 401;
  the 6th request in a day → 429.
- The backfill script in dry-run lists the existing Beta buyers.
