# Issue #15 implementation record

Status: completed and merged in PR #16 (https://github.com/kumakumapon/c_cpp_lib/pull/16).
Follow-up review fixes landed in PR #17.

## Completed scope
- [x] A-1/B-1: allocation-free test registry, always-active checks, numeric diagnostics, TAP; all 18 original suites migrated.
- [x] A-2: direct bridge string header includes.
- [x] A-3: heap allocation/reallocation/alignment overflow checks and regressions.
- [x] A-4: bounded OPC UA primitive/composite codecs, server capacity checks, malformed/truncated input tests.
- [x] A-5: nonrecursive hexdump locking and bounded log formatting.
- [x] A-6: Linux/Windows Debug/Release, CRC variants, shared libraries, ASan/UBSan and installed-consumer CI.
- [x] A-7: install/export/package configuration and namespaced aliases.
- [x] A-8/B-2: shared CRC/checksums and ring buffer; UART/Modbus migrated.
- [x] A-9: configure-dependent discovery, formatting/static-analysis configuration, string and portability cleanup.
- [x] B-3: MBAP, per-connection TCP framing, transaction IDs, RTU-over-TCP, bridge register integration.
- [x] B-4: timer wheel, monotonic clock hook, cooperative round-robin tasks, deadline accounting.
- [x] B-5: March C-, ROM CRC, CPU hook, stack watermark, watchdog and clock checks.
- [x] B-6: saturating Q15/Q31 arithmetic, integer math, generic numeric/control adapters.
- [x] API documentation, migration notes and local validation.

## Validation evidence
- MSVC Debug/Release: 25 CTest entries passed.
- MSVC CRC tables enabled: 25 entries passed.
- GCC Release on Ubuntu 24.04: 25 entries passed.
- Installed C++ consumer and C-only consumer both built and ran successfully.
- CI on implementation commits covers all 11 jobs listed above; final status is recorded in PR checks.
- The expected-failure NDEBUG test passes only when an intentional check failure returns nonzero.
- No remaining raw assert calls in test sources; git diff --check clean.

## API constraints
OPC UA binary APIs now require explicit buffer lengths (source-level breaking change).
CPU register self-tests require target-specific hooks; this code does not certify functional safety.
Ring buffers and shared protocol state require caller synchronization; timer callbacks are cooperative.
Modbus networking and RTU-over-TCP frame boundaries remain transport-adapter responsibilities.
Previously proposed libraries mentioned only as background in #15 are outside this issue's scope.
