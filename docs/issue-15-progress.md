# Issue #15 implementation checkpoint

Scope: A-1 through A-9, B-1 through B-6, and shared ring buffer extraction.
Previously proposed libraries listed only as background in #15 are not part of this change.

## Status
- [x] Requirements inspected; implementation not started at initial checkpoint.
- [ ] A-1/B-1: always-active, allocation-free test framework; migrate tests.
- [ ] A-2/A-3/A-4/A-5: bridge includes, heap overflow, bounded OPC UA codecs, logger locking.
- [ ] A-6/A-7/A-9: CI, install/export/aliases, formatting and build hygiene.
- [ ] A-8/B-2: shared CRC/checksums and ring buffer; migrate protocol consumers.
- [ ] B-3: Modbus TCP and RTU-over-TCP transport.
- [ ] B-4: timer wheel and cooperative scheduler.
- [ ] B-5: portable self-test facilities and hardware hooks.
- [ ] B-6: Q15/Q31 arithmetic, math and generic C++ adapters.
- [ ] Debug/Release, sanitizers, installed consumer checks and final review.

## Resume
1. Check out feature/issue-15-quality-foundation and inspect git status/log.
2. Read this checkpoint and the draft PR for completed work and validation results.
3. Continue unchecked items; commit and push coherent milestones.
4. Update documentation and PR with actual tests and limitations; mark ready only when complete.

Hardware-specific CPU register diagnostics use target-provided hooks; host tests cannot certify functional safety.
