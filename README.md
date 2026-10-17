# cake-snapshot

Read the exact configured bandwidth of **1–64 root CAKE qdiscs** in one
read-only rtnetlink dump. Intended for SQM/autorate monitors, multi-WAN admission
controllers and diagnostics on OpenWrt or Linux. No daemon, shell commands,
libnl dependency, NSS driver or fixed interface naming convention is needed.

Repeated `tc qdisc show dev …` calls can each request a global qdisc dump.
Batching the selected roots avoids repeated traversal and process startup.
This tool reads the kernel's byte rate directly, without depending on human
rate formatting or JSON plugins for unrelated hardware qdiscs.

## Build and use

Requires a C11 compiler, libc and Linux UAPI headers with CAKE definitions.

```sh
make
./build/cake-snapshot pppoe-wan ifb4pppoe-wan
./build/cake-snapshot --timeout-ms 750 eth0 vlan.123
make install DESTDIR=/path/to/staging
```

`make install` defaults to `/usr/bin`; use a staging directory to review files.
The reader can normally run without root privileges. It sends only
`RTM_GETQDISC` and does not create interfaces, change queues or inject traffic.

Output example (decimal strings preserve all uint64 bits for JSON consumers):

```json
{"schema_version":1,"duration_ms":1,"rates":[{"dev":"pppoe-wan","ifindex":8,"bytes_per_second":"4055000","bits_per_second":"32440000"}]}
```

Rates are **configured limits, not measured throughput or available capacity**.
`duration_ms` uses a monotonic clock and covers collection and identity checks,
before serialization. Interface order matches the arguments. Arbitrary ASCII
Linux interface names up to 15 bytes are accepted, including JSON-escaped
quotes/backslashes; whitespace, colons, slashes and non-ASCII are rejected.
Use `--` before an interface name beginning with a dash.

## Failure contract

Exit 0 means a complete, validated snapshot. Exit 1 means collection failed;
exit 2 means invalid arguments. On collection failure no JSON is printed.
Consumers must check the exit status, discard incomplete output and enforce
their own freshness and conservative fallback policy. Output I/O failures can
leave partial bytes; always check exit status as well as JSON validity.

Missing/duplicate roots, a non-CAKE requested root, unlimited/zero rates,
bit-rate conversion overflow, malformed attributes, truncated/interrupted
dumps, wrong sequence/sender and deadline expiry are rejected. All requested
interfaces are resolved again before publication. Unrelated qdiscs are not
interpreted. Only `cake` is supported: `cake_mq` and CAKE attached below a class
are intentionally outside this version's root-only contract.

The default collection deadline is 750 ms (`--timeout-ms 1..60000`). Late
results are rejected. This is a user-space deadline, not a hard guarantee
against kernel stalls or scheduling delays. A dump is not an atomic transaction
across interfaces; an autorate controller may update rates during collection.
The output does not prove hardware offload, queue latency or game performance.

## Tests

```sh
make test          # actual decoder, malformed netlink and CLI tests
make sanitize      # ASan + UBSan on the actual decoder
make integration   # unshare user+network namespace; real CAKE, 64 dummy devices
```

Integration requires `ip`, `tc`, Python 3, user namespaces, and kernel CAKE/dummy
support. It refuses to run without evidence of a separate network namespace.
No production router is involved. The CI runs glibc tests/sanitizers/kernel
integration and a musl build/test job. See [VALIDATION.md](VALIDATION.md) for
executed results and limits; CI configuration alone is not a passing CI result.

Cross compilation, for example with an existing OpenWrt toolchain:

```sh
make CC=/path/to/aarch64-openwrt-linux-musl-gcc
```

Use a clean/separate build directory when changing compilers (`make clean`).

## OpenWrt packaging

```sh
make openwrt-package
# Copy build/openwrt/cake-snapshot into a separate SDK's package/ directory.
# Inside that SDK:
make package/cake-snapshot/compile V=s
```

The generated local-source package contains the same source and license.
It installs only `/usr/bin/cake-snapshot`; no init script or configuration is
installed. Full SDK package compilation is a separate check from cross compiling
the binary. Never use a live production build tree as an experiment.

## Integration policy

A monitor should execute one snapshot for all of its CAKE roots, check success
and timestamp it on receipt. Avoid retry loops that retain old data as fresh.
For hardware-assisted routing, keep conntrack identity, PBR/NAT affinity,
firmware receipts, software fallback and telemetry freshness checks in the
controller; this reader does not implement or relax them.

## Origin and license

Derived from Athena NSS's read-only CAKE budget reader, generalized in an
independent workspace. [PROVENANCE.md](PROVENANCE.md) records exact input hashes
and changes. GPL-2.0-or-later; see [COPYING](COPYING).
