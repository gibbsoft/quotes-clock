# Security Policy

Quotes Clock is a local-network firmware project. Please do not publish
vulnerability details before maintainers have had time to assess and fix them.

## Reporting

For now, report security issues privately to the repository owner or maintainer.
Once the project moves to its public GitHub home, this file should be updated
with the preferred private reporting channel.

## Current Security Notes

- The native HTTPS server currently uses build-time bootstrap TLS material.
  Devices flashed from the same artifact share the same TLS identity.
- First-run setup is intentionally unauthenticated until an admin password is
  configured.
- Admin password storage is salted SHA-256 and is listed for future hardening.
- Do not commit `.env`, TLS private keys, stock firmware backups, or package
  tokens.
