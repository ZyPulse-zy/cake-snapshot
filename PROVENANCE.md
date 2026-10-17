# Provenance

Input: Athena NSS `code/controller/native/cake_budget_read.c` from the local
2026-10-17 delivery source snapshot, SHA-256:
`6647617e28b82fa233b6fad116d03d46bfb5363f4524326cecd9b863d16c0207`.
That file declares GPL-2.0-or-later. The original project is
https://github.com/ZyPulse-zy/athena-nss-mainline; local delivery snapshots can
contain changes beyond the public branch. No claim is made that this digest is
currently downloadable from a public Git commit.

The original reader requested ten/twelve `rpwanN`/`rpifbN` roots and enforced
Athena's 1–200 Mbit/s queue contract. This independent version:

- accepts 1–64 explicit interface names in the current network namespace;
- resolves names using libc's interface API, without relying on mounted sysfs;
- supports finite rates from one byte/s through `UINT64_MAX/8` bytes/s;
- provides exact decimal-string byte/bit rates and a versioned JSON schema;
- adds configurable deadlines, nonblocking socket and a sticky decoder failure;
- escapes names for JSON and rejects invalid CLI inputs before collection;
- supplies independent tests, CI, staging install and local OpenWrt packaging.

Synthetic decoder tests derive from the original GPL test fixture, extended for
all supported interface counts and generic rates. Original project files,
router files/configuration and services were read-only throughout this work.
No private addresses, state dumps, credentials or binaries are part of this
repository. Synthetic names and documentation examples are not deployment data.
