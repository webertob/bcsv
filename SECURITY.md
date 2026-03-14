# Security Policy

## Reporting a Vulnerability

If you discover a security vulnerability in BCSV, please report it responsibly:

1. **Do NOT open a public GitHub issue** for security vulnerabilities
2. Use [GitHub Security Advisories](https://github.com/webertob/bcsv/security/advisories/new) to report privately
3. Alternatively, contact the maintainer directly via the email listed in the repository

## Response Timeline

- **Acknowledge:** Within 7 days of report
- **Assessment:** Within 14 days — determine severity and affected versions
- **Fix:** Coordinated disclosure with patch release

## Supported Versions

| Version | Supported          |
|---------|--------------------|
| 1.4.x   | ✅ Active          |
| 1.3.x   | ⚠️ Security fixes only |
| < 1.3   | ❌ End of life     |

## Scope

BCSV is a file format library. Security concerns primarily involve:

- Buffer overflows from malformed files
- Denial of service from crafted inputs (e.g., excessive allocation)
- Integer overflow in codec/sampler subsystems

The library does not handle network I/O, authentication, or encryption.
