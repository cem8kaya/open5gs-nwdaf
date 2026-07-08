# Security Policy

## Supported Versions

Security fixes are applied to the latest released minor version and the `main`
branch. Older tags are not maintained.

| Version | Supported |
|---|---|
| `1.x` (latest) | ✅ |
| `main` | ✅ |
| < `1.0` | ❌ |

## Reporting a Vulnerability

**Please do not report security vulnerabilities through public GitHub issues,
discussions, or pull requests.**

Instead, use one of the following private channels:

1. **GitHub Private Vulnerability Reporting** (preferred) — go to the
   [Security tab](https://github.com/cem8kaya/open5gs-nwdaf/security/advisories/new)
   and open a private advisory.
2. **Email** — send details to **tcckaya8@gmail.com** with the subject line
   `[SECURITY] open5gs-nwdaf`.

Please include as much of the following as you can:

- A description of the vulnerability and its impact
- Steps to reproduce (proof-of-concept if possible)
- Affected version / commit and build configuration (which optional features
  were enabled: TLS, OAuth2, MongoDB, SQLite)
- Any suggested mitigation

## What to Expect

- **Acknowledgement** within **72 hours**.
- An initial assessment and severity rating within **7 days**.
- We will keep you informed of remediation progress and coordinate a disclosure
  timeline with you. We aim to ship a fix within **90 days**, sooner for
  high-severity issues.
- With your permission, we will credit you in the release notes and advisory.

Please practice **coordinated disclosure**: give us a reasonable window to
release a fix before any public disclosure.

## Scope

This project is a network function that sits on a 5G core's service-based
interface. Security-relevant areas include, but are not limited to:

- The SBI HTTP surface (`NwdafServer`) — request parsing, path handling, JSON
  deserialization
- Authentication and transport: OAuth 2.0 bearer-token validation, TLS/mTLS
  configuration (TS 33.501 §13.3)
- Rate limiting (token bucket) bypass
- Data collection paths that read from the host (journald parsing, `/proc`,
  `/sys`, MongoDB queries)
- Model persistence (path handling, deserialization of ML model files)
- Denial of service against the collector or analytics engine

## Deployment Hardening Notes

The project ships with several defensive defaults, but operators are responsible
for their deployment posture:

- **Enable TLS/mTLS** (`tls_enabled: true`) and **OAuth2** for any non-lab
  deployment — the SBI carries analytics that can expose network state.
- **Bind to a trusted interface** (`sbi_bind_address`) — do not expose port
  `7779` to untrusted networks.
- **Keep rate limits on** (`rate_limit_per_ip_rps`, `rate_limit_global_rps`).
- **Protect `model_dir` and `history_db_path`** with appropriate filesystem
  permissions — the systemd unit runs with hardening directives; review them
  for your environment.
- **Redact subscriber identifiers** (IMSI/SUPI) before sharing logs or configs.

The build already enables `-D_FORTIFY_SOURCE=2`, `-fstack-protector-strong`,
PIE, and full RELRO; keep these on.

Thank you for helping keep the Open5GS ecosystem secure. 🛡️
