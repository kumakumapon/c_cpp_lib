# Issue #15 implementation checkpoint

Scope: A-1 through A-9, B-1 through B-6, and shared ring buffer extraction.
Previously proposed libraries listed only as background in #15 are not part of this change.

## Status
- [x] Requirements inspected; implementation not started at initial checkpoint.
- [x] A-1/B-1: always-active, allocation-free test framework; migrate tests.
- [x] A-2/A-3/A-4/A-5: bridge includes, heap overflow, bounded OPC UA codecs, logger locking.
- [x] A-6/A-7/A-9: CI, install/export/aliases, formatting and build hygiene.
- [x] A-8/B-2: shared CRC/checksums and ring buffer; migrate protocol consumers.
- [x] B-3: Modbus TCP and RTU-over-TCP transport.
- [x] B-4: timer wheel and cooperative scheduler.
- [x] B-5: portable self-test facilities and hardware hooks.
- [x] B-6: Q15/Q31 arithmetic, math and generic C++ adapters.
- [ ] Debug/Release, sanitizers, installed consumer checks and final review.

## Resume
1. Check out feature/issue-15-quality-foundation and inspect git status/log.
2. Read this checkpoint and the draft PR for completed work and validation results.
3. Continue unchecked items; commit and push coherent milestones.
4. Update documentation and PR with actual tests and limitations; mark ready only when complete.

Hardware-specific CPU register diagnostics use target-provided hooks; host tests cannot certify functional safety.

## Checkpoint 2026-09-22
Implemented first pass of A-1..A-9 and B-1..B-6, including shared ring buffer.
MSVC Release: 24 suites passed before boundary regression additions.
Added explicit NDEBUG expected-failure test, boundary regressions, CI and install consumer.
Remaining: run full validation, inspect Linux/ASan results, complete bridge TCP integration,
review edge cases, document APIs/constraints and replace this progress checklist with final evidence.
Untracked *_issue15.py scripts are one-shot editing helpers: do not rerun them.


## Review checkpoint
All scoped implementations and API documentation are present; bridge TCP integration is tested.
Local MSVC Debug/Release and GCC Release each passed 25 CTest entries.
Initial commit CI passed all 11 jobs, including ASan/UBSan and installed consumers.
Added full OPC UA scalar/NodeId truncation and deterministic malformed-input tests, scheduler
callback cancellation/overflow tests and Q15/Q31 generic PID tests after that CI run.
Remaining: verify final CI on this updated code, then mark PR ready for review.
