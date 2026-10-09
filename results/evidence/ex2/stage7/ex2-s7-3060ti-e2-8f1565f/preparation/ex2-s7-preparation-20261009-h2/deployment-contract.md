# Stage-7 deployment contract — preparation only

Execution root: C:\Users\rolan\src\ComputeLab-Stage7-3060Ti. Required detached, clean source: 8f1565f7fb92e2184f4d2382aabf8fa7d00e1274. Linked common directory remains in the original repository. The archival checkout and historical evidence must remain untouched.

Reuse the accepted Stage-6 protocol 1.4 engine without tracked modifications. Parser-owned manifest type remains ex2-stage6-diagnostic, manifest version 1, evidence schema 2, diagnostic kind, H only. Final filename must be results/local/<manifest-id>-stage6-manifest.json.

All 22 frozen A1/A2/B1/B2/C/D1/E1/E2 cells; E3 interprets existing timings and adds no cell. No D2/extensions. Five paired fresh-process blocks per cell in CV/VC/CV/VC/CV order, ten children per cell, 220 children total. Each successful child records exactly 100 ordered H observations at indices 0..99, zero warm-up: 22,000 planned records. Preserve full correctness verification, raw failures and extreme timings. No outlier deletion, extra warm-up, timing gate, adaptive count, retry or resume. Continue only by resolved-only-no-retry protocol-1.4 rules. Deadlines: operation 60000 ms, child 1200000 ms, campaign 86400000 ms.

Proposed machine ID: ex2-s7-3060ti-machine. Proposed manifest ID: ex2-s7-3060ti-e1-8f1565f. These are distinct from Stage-6 identities and fit the inspected parser's anonymous character restrictions, machine maximum 96 and manifest maximum 87. Adoption requires namespace collision checks and all preceding gates.

Target obligation: independently capture CUDA ordinal and Vulkan physical-device index and API UUIDs, requiring both UUIDs equal 99a2369e-ca50-aac6-5c2c-fcaa44625084 on NVIDIA GeForce RTX 3060 Ti, compute capability 8.6. Do not infer selectors from ordering or nvidia-smi alone.

Explicit accepted limitation: prior Debug session was operator-interrupted after 924 PASS of 1242 discovered tests, one interrupted and 317 not started. The canonical suite DID NOT PASS and remains INCOMPLETE. It will NOT be rerun. Only the three named Debug GPU smokes are authorized, once after one required Debug build; exactly four existing direct and four existing supervisor Release smokes are authorized after one Release build. These provide bounded functional/control evidence only.

Freeze source and exactly eight actual Release artifacts only after every smoke passes. Independently verify candidate, final and stability; use canonical UTF-8 without BOM/CR and one final LF, semantic SHA-256 excluding manifest_sha256 and separate physical SHA-256. Check 665 production destinations and final manifest absence. Publish create-new only. After freeze no rebuild, relink, shader compilation, tracked edits, selector or toolchain changes.

Gate 0 remains FAIL for proposed D1/H comparison; Stage-2 performance execution NOT GRANTED; production backend UNSELECTED. No backend or cross-generation ranking is admitted.

Human review must cover live desktop/remote-control activity, power/clock state, Vulkan layers, external instrumentation, sleep/update policies and incomplete Debug coverage. Do not change processes, clocks, layers, registry or machine policy automatically.

CAMPAIGN NOT EXECUTED. LAUNCH NOT AUTHORIZED. No production supervisor invocation, production slots, historical launcher, automation, commits, pushes or external writes are authorized. Any failed gate stops preparation; retain evidence without automatic retry.
