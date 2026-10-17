# Executed validation

2026-10-17, independent Linux development workspace. No deployment performed.

| Check | Actual result |
| --- | --- |
| GCC 15.2.0/glibc, C11, `-Wall -Wextra -Werror` | Passed |
| Actual decoder on synthetic netlink input | 4,597 checks passed |
| ASan + UBSan decoder | 4,597 checks passed, no diagnostics |
| CLI tests (help, invalid arguments, incomplete snapshots) | All 3 groups passed |
| Real kernel integration in fresh user/network namespace | 64 dummy devices; 7 complete snapshots; 4 rejection cases passed |
| Compare reader output to real `tc -j` kernel rates | Exact byte rate and bit-rate conversion; selected root sets of 1, 2, 5, 10, 12, 64 |
| Qdisc state before/after six successful reads | Identical |
| Syscall inspection | One RTM_GETQDISC dump request, no configuration request |
| OpenWrt GCC 14.4.0/AArch64/musl cross build | Passed |
| AArch64 decoder under QEMU 10.2.1 | 4,597 checks passed |
| Independent SDK full local-source package build | `cake-snapshot-0.1.0-r1.apk` produced for aarch64_cortex-a53 |
| Executable extracted from that APK under QEMU | All 3 CLI groups passed |
| QEMU user-mode real netlink integration | Blocked: QEMU rejects RTM_GETQDISC `sendto` with EOPNOTSUPP; no decoder failure or target hardware result claimed |

The SDK was copied into this chat's independent workspace. Its original files
were not modified. Missing host-tool links were repaired in the copy only;
GNU awk was extracted locally. Nothing was installed on the host or router.

The tests initially found an incorrectly escaped test string and a namespace
check that assumed `/proc/1/ns/net` was readable inside an unprivileged user
namespace. Those test-harness errors were fixed before the passing run. A
first QEMU package launch used a staging path cleaned by the SDK; the APK was
then extracted and the actual packaged executable tested.

These results do not demonstrate end-to-end game quality, NSS acceleration,
long-term router stability, all OpenWrt platforms or remote CI success. Current
CI run status must be checked on GitHub; merely adding the workflow is not proof.
