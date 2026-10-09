# E2 independent full-campaign post-run audit

{
  "packages_verified": 220,
  "raw_rows_verified": 22000,
  "correctness_valid_rows": 22000,
  "progress_events_verified": 44000,
  "logical_cells_regenerated": 22,
  "process_summaries_regenerated": 220,
  "windows_regenerated": 880,
  "cell_analyses_regenerated": 22,
  "campaign_analyses_regenerated": 1,
  "cleanup_slots": []
}

## 1. Audit identity, independence and evidence authority

Independent newly written Python/NumPy audit, pinned source consulted as contract. No producer serializer, parser, oracle or verifier executed. Read-only source and retained data; CPU reconstruction only.

## 2. Exact E2 frozen identities

Campaign ex2-s7-3060ti-e2-8f1565f; source 8f1565f7fb92e2184f4d2382aabf8fa7d00e1274; machine ex2-s7-3060ti-machine; RTX 3060 Ti, UUID 99a2369e-ca50-aac6-5c2c-fcaa44625084; CUDA0/Vulkan0. Semantic manifest 99404552f6302a7273f991d02d6532ebd06b38a6e7224c17721278d6b00899aa; physical manifest d4d93217721285bbc7d55d8fc6853a3685e0d23f1f5e1b6203244c359122e1e1.

{
  "a1": {
    "path": "out/build/x64-release/src/vulkan/Ex2A1.comp.spv",
    "sha256": "c40c5ef608254d7763aa244d1ca6403605f750212a038ccf1913cff2bd61c63a",
    "size_bytes": 1552
  },
  "a2": {
    "path": "out/build/x64-release/src/vulkan/Ex2A2.comp.spv",
    "sha256": "bfda73b6abe562facaba4fa265b281144e2c8761517ffad71c8176184cf17f3f",
    "size_bytes": 2188
  },
  "b1": {
    "path": "out/build/x64-release/src/vulkan/Ex2B1.comp.spv",
    "sha256": "7eb4a7d40c2ea5aa133821290318b18b45c49f5284cb6ffc8ebda5f9b35e933c",
    "size_bytes": 1880
  },
  "b2": {
    "path": "out/build/x64-release/src/vulkan/Ex2B2.comp.spv",
    "sha256": "2761fe5fab23630ce7ed47a93afe74cd769d1b154fa13a33a49601ed02120a26",
    "size_bytes": 1880
  },
  "c": {
    "path": "out/build/x64-release/src/vulkan/Ex2C.comp.spv",
    "sha256": "7e4643308689eb49ee19a205ad126cf616e57f2c613b2e11007ad5a95446a4aa",
    "size_bytes": 1436
  },
  "child": {
    "path": "out/build/x64-release/src/app/ComputeLabEx2Stage6.exe",
    "sha256": "4104e5f7237b03d5ac3265f450358e76533b099e4fdc049e62cd9ff20e9ae93c",
    "size_bytes": 766464
  },
  "d1": {
    "path": "out/build/x64-release/src/vulkan/Ex2D1.comp.spv",
    "sha256": "3d0787c7ce83802caca54cd9bc107d506949a509ba0ffe5f0604bf72ed9e7d61",
    "size_bytes": 1912
  },
  "supervisor": {
    "path": "out/build/x64-release/src/app/ComputeLabEx2Stage6Supervisor.exe",
    "sha256": "33a0bebcba66e02e2f39ee4b311844c748e1a9b0c828a9c49b5e57ea989de5c8",
    "size_bytes": 659456
  }
}

## 3. Preparation and original human authorization chain

See individually checked candidate/final/precreation, exact operator authorization and shared Phase-A artifact authority rows in the check matrix. Historical prelaunch not-executed state is appropriate for its timestamp.

## 4. Expected schedule and observed topology

Source-owned 22 cells x 10 children; CV/VC/CV/VC/CV; 220 declared unique routes; exact four files per package. Audit counts below include only successfully completed independent checks.

## 5. Ledger state/revision/slot consistency

Reported completed revision 905; independent transition arithmetic: running 1 + 880 slot transitions + 22 cell publications + campaign publication 1 + terminal publication 1. Exact per-slot anchors checked.

## 6. Ordinary versus DR-45 cleanup dispositions

Discovered cleanup slots: []. Ordinary and cleanup cases use separate strict predicates; no process excluded.

## 7. Progress and native control

Each checked success requires 200 Started/Returned frames, exact 40-byte source grammar, indexes, QPC constraints, clean EOF, no trailing bytes or outstanding attempt, final Job ActiveProcesses=0 and strict containment/exit/deadline predicates.

## 8. Package structures, hashes and identities

Per-slot package facts include independently framed four-artifact package hashes, condition/series identities, provenance and regenerated summary hashes. Ledger artifact anchors independently checked.

## 9. Raw H rows and correctness evidence

All independently checked rows retain original order and extremes. Validation true/status ok; unsigned timing values; exact completion=submission+wait; native interval empty/null. No filtering or benchmark interpretation.

## 10. Logical input/output reconstruction

Full mix64 inputs, all B permutations and bijections, framed B/C logical identities, N-counter histograms with inactive zeros, all D passes and final buffer parity, and all copy bytes reconstructed. Literal known answers and separately written scalar calculations check vector arithmetic.

## 11. Process summaries and positional windows

Sequential binary64 sums, two-pass sample SD, even median and nearest-rank p95 reconstructed; exact summary bytes checked. Four fixed 25-row windows per process independently reconstructed.

## 12. Cell and campaign analysis reproductions

Source field order and exact numeric serialization independently reconstructed for each analysis; compare exact bytes, length, SHA-256 and ledger relative path anchors. Within-backend process median span is the only ratio regenerated.

## 13. Claim firewall

Gate 0 FAIL; Stage 2 NOT GRANTED; production backend UNSELECTED. All canonical campaign claim flags false. No cross-API or cross-generation performance claims.

## 14. E2/E1 separation and no-retry record

Only E2 packages in this acceptance dataset. E1 permanently INCOMPLETE, revision 20 authoritative; never adopt temporary revision 21. One-launch/no-retry claims are bounded by retained records.

## 15. Before/after immutability

Primary sealed inventory: 1671 files, plus 80 shared historical preparation records. All bytes/sizes/mtimes and E2 path set checked after reads, including eight artifacts, manifest and clean detached source.

## 16. Discrepancies, uncertainties and limitations

Preliminary review-001/review-002 records are validator development diagnostics, not evidence verdicts. Original evidence was never adjusted to fit the validator. All implementation versions remain retained.

[]

Retained validation flags plus independently reconstructed expected digests are correctness evidence, not a fresh GPU correctness rerun; actual output buffers are not retained.

Progress validation independently interprets retained ledger event records; original pipe byte streams are not separately retained. QPC does not replace scientific host timing values.

No external continuous OS process monitor is claimed. One launch/no retry is established only within retained supervisor, task and wrapper evidence.

Operator NVIDIA recording-off attestation and OBS-off probe are contextual provenance, not proof of universal absence of overhead.

Avast restoration is a retained reminder unless an actual restoration attestation is present; environmental restoration is not a scientific correctness gate.

Shared Phase-A records explicitly retain incomplete canonical Debug coverage and missing full Debug build console retention; prior bounded Release smoke receipts exist, and this audit reruns no tests.

E1 remains permanently INCOMPLETE; historical shared artifact receipts are not E2 scientific evidence and historical E1 manifest identities remain distinct.

Gate 0 FAIL, Stage 2 NOT GRANTED, production backend UNSELECTED; no ranking, speedup, cross-generation or production-runtime inference.

## 17. Check matrix and receipt hashes

Counts: {"PASS": 284, "FAIL": 0, "UNVERIFIED": 0}. Exact implementation/input/output hashes are in audit-receipt.json; its own digest is returned externally to avoid a circular self hash.

## 18. FINAL VERDICT

ACCEPTED

## 19. Accepted process index

{
  "path": "C:\\Users\\rolan\\src\\ComputeLab-Stage7-3060Ti\\results\\local\\ex2-s7-3060ti-e2-8f1565f-postrun-audit-20261009\\ex2-s7-3060ti-e2-accepted-process-index.json",
  "size_bytes": 503846,
  "sha256": "187e8b3dab63bdb6a4d99611b501d039462b87a2c522a6b7f5ed7a8b6cfc1d78"
}

## 20. NEXT HUMAN-CONTROLLED STEP

Separate curation review only after acceptance; no benchmark inference

| Check | Status | Detail |
|---|---|---|
| clean detached pinned source before | PASS | None |
| sealed input identity at audit start | PASS | None |
| supplemental preparation inventory at audit start | PASS | None |
| strict canonical manifest and separately reconstructed full schedule | PASS | None |
| all eight frozen Release artifacts against manifest and receipts | PASS | None |
| terminal ledger canonical identity, revisions and preflight | PASS | None |
| E2 topology, staging absence and exact analysis set | PASS | None |
| independent literal known answers and scalar/vector equivalence | PASS | literal mix64/A1/A2/B1/B2/C/D1/E and framing; independently written scalar versus vector boundary probes |
| shortest decimal serializer known answers | PASS | None |
| cell 00 complete independent input/output reconstruction | PASS | None |
| cell 01 complete independent input/output reconstruction | PASS | None |
| cell 02 complete independent input/output reconstruction | PASS | None |
| cell 03 complete independent input/output reconstruction | PASS | None |
| cell 04 complete independent input/output reconstruction | PASS | None |
| cell 05 complete independent input/output reconstruction | PASS | None |
| cell 06 complete independent input/output reconstruction | PASS | None |
| cell 07 complete independent input/output reconstruction | PASS | None |
| cell 08 complete independent input/output reconstruction | PASS | None |
| cell 09 complete independent input/output reconstruction | PASS | None |
| cell 10 complete independent input/output reconstruction | PASS | None |
| cell 11 complete independent input/output reconstruction | PASS | None |
| cell 12 complete independent input/output reconstruction | PASS | None |
| cell 13 complete independent input/output reconstruction | PASS | None |
| cell 14 complete independent input/output reconstruction | PASS | None |
| cell 15 complete independent input/output reconstruction | PASS | None |
| cell 16 complete independent input/output reconstruction | PASS | None |
| cell 17 complete independent input/output reconstruction | PASS | None |
| cell 18 complete independent input/output reconstruction | PASS | None |
| cell 19 complete independent input/output reconstruction | PASS | None |
| cell 20 complete independent input/output reconstruction | PASS | None |
| cell 21 complete independent input/output reconstruction | PASS | None |
| slot 000 package/control/progress/raw/summary/windows | PASS | None |
| slot 001 package/control/progress/raw/summary/windows | PASS | None |
| slot 002 package/control/progress/raw/summary/windows | PASS | None |
| slot 003 package/control/progress/raw/summary/windows | PASS | None |
| slot 004 package/control/progress/raw/summary/windows | PASS | None |
| slot 005 package/control/progress/raw/summary/windows | PASS | None |
| slot 006 package/control/progress/raw/summary/windows | PASS | None |
| slot 007 package/control/progress/raw/summary/windows | PASS | None |
| slot 008 package/control/progress/raw/summary/windows | PASS | None |
| slot 009 package/control/progress/raw/summary/windows | PASS | None |
| slot 010 package/control/progress/raw/summary/windows | PASS | None |
| slot 011 package/control/progress/raw/summary/windows | PASS | None |
| slot 012 package/control/progress/raw/summary/windows | PASS | None |
| slot 013 package/control/progress/raw/summary/windows | PASS | None |
| slot 014 package/control/progress/raw/summary/windows | PASS | None |
| slot 015 package/control/progress/raw/summary/windows | PASS | None |
| slot 016 package/control/progress/raw/summary/windows | PASS | None |
| slot 017 package/control/progress/raw/summary/windows | PASS | None |
| slot 018 package/control/progress/raw/summary/windows | PASS | None |
| slot 019 package/control/progress/raw/summary/windows | PASS | None |
| slot 020 package/control/progress/raw/summary/windows | PASS | None |
| slot 021 package/control/progress/raw/summary/windows | PASS | None |
| slot 022 package/control/progress/raw/summary/windows | PASS | None |
| slot 023 package/control/progress/raw/summary/windows | PASS | None |
| slot 024 package/control/progress/raw/summary/windows | PASS | None |
| slot 025 package/control/progress/raw/summary/windows | PASS | None |
| slot 026 package/control/progress/raw/summary/windows | PASS | None |
| slot 027 package/control/progress/raw/summary/windows | PASS | None |
| slot 028 package/control/progress/raw/summary/windows | PASS | None |
| slot 029 package/control/progress/raw/summary/windows | PASS | None |
| slot 030 package/control/progress/raw/summary/windows | PASS | None |
| slot 031 package/control/progress/raw/summary/windows | PASS | None |
| slot 032 package/control/progress/raw/summary/windows | PASS | None |
| slot 033 package/control/progress/raw/summary/windows | PASS | None |
| slot 034 package/control/progress/raw/summary/windows | PASS | None |
| slot 035 package/control/progress/raw/summary/windows | PASS | None |
| slot 036 package/control/progress/raw/summary/windows | PASS | None |
| slot 037 package/control/progress/raw/summary/windows | PASS | None |
| slot 038 package/control/progress/raw/summary/windows | PASS | None |
| slot 039 package/control/progress/raw/summary/windows | PASS | None |
| slot 040 package/control/progress/raw/summary/windows | PASS | None |
| slot 041 package/control/progress/raw/summary/windows | PASS | None |
| slot 042 package/control/progress/raw/summary/windows | PASS | None |
| slot 043 package/control/progress/raw/summary/windows | PASS | None |
| slot 044 package/control/progress/raw/summary/windows | PASS | None |
| slot 045 package/control/progress/raw/summary/windows | PASS | None |
| slot 046 package/control/progress/raw/summary/windows | PASS | None |
| slot 047 package/control/progress/raw/summary/windows | PASS | None |
| slot 048 package/control/progress/raw/summary/windows | PASS | None |
| slot 049 package/control/progress/raw/summary/windows | PASS | None |
| slot 050 package/control/progress/raw/summary/windows | PASS | None |
| slot 051 package/control/progress/raw/summary/windows | PASS | None |
| slot 052 package/control/progress/raw/summary/windows | PASS | None |
| slot 053 package/control/progress/raw/summary/windows | PASS | None |
| slot 054 package/control/progress/raw/summary/windows | PASS | None |
| slot 055 package/control/progress/raw/summary/windows | PASS | None |
| slot 056 package/control/progress/raw/summary/windows | PASS | None |
| slot 057 package/control/progress/raw/summary/windows | PASS | None |
| slot 058 package/control/progress/raw/summary/windows | PASS | None |
| slot 059 package/control/progress/raw/summary/windows | PASS | None |
| slot 060 package/control/progress/raw/summary/windows | PASS | None |
| slot 061 package/control/progress/raw/summary/windows | PASS | None |
| slot 062 package/control/progress/raw/summary/windows | PASS | None |
| slot 063 package/control/progress/raw/summary/windows | PASS | None |
| slot 064 package/control/progress/raw/summary/windows | PASS | None |
| slot 065 package/control/progress/raw/summary/windows | PASS | None |
| slot 066 package/control/progress/raw/summary/windows | PASS | None |
| slot 067 package/control/progress/raw/summary/windows | PASS | None |
| slot 068 package/control/progress/raw/summary/windows | PASS | None |
| slot 069 package/control/progress/raw/summary/windows | PASS | None |
| slot 070 package/control/progress/raw/summary/windows | PASS | None |
| slot 071 package/control/progress/raw/summary/windows | PASS | None |
| slot 072 package/control/progress/raw/summary/windows | PASS | None |
| slot 073 package/control/progress/raw/summary/windows | PASS | None |
| slot 074 package/control/progress/raw/summary/windows | PASS | None |
| slot 075 package/control/progress/raw/summary/windows | PASS | None |
| slot 076 package/control/progress/raw/summary/windows | PASS | None |
| slot 077 package/control/progress/raw/summary/windows | PASS | None |
| slot 078 package/control/progress/raw/summary/windows | PASS | None |
| slot 079 package/control/progress/raw/summary/windows | PASS | None |
| slot 080 package/control/progress/raw/summary/windows | PASS | None |
| slot 081 package/control/progress/raw/summary/windows | PASS | None |
| slot 082 package/control/progress/raw/summary/windows | PASS | None |
| slot 083 package/control/progress/raw/summary/windows | PASS | None |
| slot 084 package/control/progress/raw/summary/windows | PASS | None |
| slot 085 package/control/progress/raw/summary/windows | PASS | None |
| slot 086 package/control/progress/raw/summary/windows | PASS | None |
| slot 087 package/control/progress/raw/summary/windows | PASS | None |
| slot 088 package/control/progress/raw/summary/windows | PASS | None |
| slot 089 package/control/progress/raw/summary/windows | PASS | None |
| slot 090 package/control/progress/raw/summary/windows | PASS | None |
| slot 091 package/control/progress/raw/summary/windows | PASS | None |
| slot 092 package/control/progress/raw/summary/windows | PASS | None |
| slot 093 package/control/progress/raw/summary/windows | PASS | None |
| slot 094 package/control/progress/raw/summary/windows | PASS | None |
| slot 095 package/control/progress/raw/summary/windows | PASS | None |
| slot 096 package/control/progress/raw/summary/windows | PASS | None |
| slot 097 package/control/progress/raw/summary/windows | PASS | None |
| slot 098 package/control/progress/raw/summary/windows | PASS | None |
| slot 099 package/control/progress/raw/summary/windows | PASS | None |
| slot 100 package/control/progress/raw/summary/windows | PASS | None |
| slot 101 package/control/progress/raw/summary/windows | PASS | None |
| slot 102 package/control/progress/raw/summary/windows | PASS | None |
| slot 103 package/control/progress/raw/summary/windows | PASS | None |
| slot 104 package/control/progress/raw/summary/windows | PASS | None |
| slot 105 package/control/progress/raw/summary/windows | PASS | None |
| slot 106 package/control/progress/raw/summary/windows | PASS | None |
| slot 107 package/control/progress/raw/summary/windows | PASS | None |
| slot 108 package/control/progress/raw/summary/windows | PASS | None |
| slot 109 package/control/progress/raw/summary/windows | PASS | None |
| slot 110 package/control/progress/raw/summary/windows | PASS | None |
| slot 111 package/control/progress/raw/summary/windows | PASS | None |
| slot 112 package/control/progress/raw/summary/windows | PASS | None |
| slot 113 package/control/progress/raw/summary/windows | PASS | None |
| slot 114 package/control/progress/raw/summary/windows | PASS | None |
| slot 115 package/control/progress/raw/summary/windows | PASS | None |
| slot 116 package/control/progress/raw/summary/windows | PASS | None |
| slot 117 package/control/progress/raw/summary/windows | PASS | None |
| slot 118 package/control/progress/raw/summary/windows | PASS | None |
| slot 119 package/control/progress/raw/summary/windows | PASS | None |
| slot 120 package/control/progress/raw/summary/windows | PASS | None |
| slot 121 package/control/progress/raw/summary/windows | PASS | None |
| slot 122 package/control/progress/raw/summary/windows | PASS | None |
| slot 123 package/control/progress/raw/summary/windows | PASS | None |
| slot 124 package/control/progress/raw/summary/windows | PASS | None |
| slot 125 package/control/progress/raw/summary/windows | PASS | None |
| slot 126 package/control/progress/raw/summary/windows | PASS | None |
| slot 127 package/control/progress/raw/summary/windows | PASS | None |
| slot 128 package/control/progress/raw/summary/windows | PASS | None |
| slot 129 package/control/progress/raw/summary/windows | PASS | None |
| slot 130 package/control/progress/raw/summary/windows | PASS | None |
| slot 131 package/control/progress/raw/summary/windows | PASS | None |
| slot 132 package/control/progress/raw/summary/windows | PASS | None |
| slot 133 package/control/progress/raw/summary/windows | PASS | None |
| slot 134 package/control/progress/raw/summary/windows | PASS | None |
| slot 135 package/control/progress/raw/summary/windows | PASS | None |
| slot 136 package/control/progress/raw/summary/windows | PASS | None |
| slot 137 package/control/progress/raw/summary/windows | PASS | None |
| slot 138 package/control/progress/raw/summary/windows | PASS | None |
| slot 139 package/control/progress/raw/summary/windows | PASS | None |
| slot 140 package/control/progress/raw/summary/windows | PASS | None |
| slot 141 package/control/progress/raw/summary/windows | PASS | None |
| slot 142 package/control/progress/raw/summary/windows | PASS | None |
| slot 143 package/control/progress/raw/summary/windows | PASS | None |
| slot 144 package/control/progress/raw/summary/windows | PASS | None |
| slot 145 package/control/progress/raw/summary/windows | PASS | None |
| slot 146 package/control/progress/raw/summary/windows | PASS | None |
| slot 147 package/control/progress/raw/summary/windows | PASS | None |
| slot 148 package/control/progress/raw/summary/windows | PASS | None |
| slot 149 package/control/progress/raw/summary/windows | PASS | None |
| slot 150 package/control/progress/raw/summary/windows | PASS | None |
| slot 151 package/control/progress/raw/summary/windows | PASS | None |
| slot 152 package/control/progress/raw/summary/windows | PASS | None |
| slot 153 package/control/progress/raw/summary/windows | PASS | None |
| slot 154 package/control/progress/raw/summary/windows | PASS | None |
| slot 155 package/control/progress/raw/summary/windows | PASS | None |
| slot 156 package/control/progress/raw/summary/windows | PASS | None |
| slot 157 package/control/progress/raw/summary/windows | PASS | None |
| slot 158 package/control/progress/raw/summary/windows | PASS | None |
| slot 159 package/control/progress/raw/summary/windows | PASS | None |
| slot 160 package/control/progress/raw/summary/windows | PASS | None |
| slot 161 package/control/progress/raw/summary/windows | PASS | None |
| slot 162 package/control/progress/raw/summary/windows | PASS | None |
| slot 163 package/control/progress/raw/summary/windows | PASS | None |
| slot 164 package/control/progress/raw/summary/windows | PASS | None |
| slot 165 package/control/progress/raw/summary/windows | PASS | None |
| slot 166 package/control/progress/raw/summary/windows | PASS | None |
| slot 167 package/control/progress/raw/summary/windows | PASS | None |
| slot 168 package/control/progress/raw/summary/windows | PASS | None |
| slot 169 package/control/progress/raw/summary/windows | PASS | None |
| slot 170 package/control/progress/raw/summary/windows | PASS | None |
| slot 171 package/control/progress/raw/summary/windows | PASS | None |
| slot 172 package/control/progress/raw/summary/windows | PASS | None |
| slot 173 package/control/progress/raw/summary/windows | PASS | None |
| slot 174 package/control/progress/raw/summary/windows | PASS | None |
| slot 175 package/control/progress/raw/summary/windows | PASS | None |
| slot 176 package/control/progress/raw/summary/windows | PASS | None |
| slot 177 package/control/progress/raw/summary/windows | PASS | None |
| slot 178 package/control/progress/raw/summary/windows | PASS | None |
| slot 179 package/control/progress/raw/summary/windows | PASS | None |
| slot 180 package/control/progress/raw/summary/windows | PASS | None |
| slot 181 package/control/progress/raw/summary/windows | PASS | None |
| slot 182 package/control/progress/raw/summary/windows | PASS | None |
| slot 183 package/control/progress/raw/summary/windows | PASS | None |
| slot 184 package/control/progress/raw/summary/windows | PASS | None |
| slot 185 package/control/progress/raw/summary/windows | PASS | None |
| slot 186 package/control/progress/raw/summary/windows | PASS | None |
| slot 187 package/control/progress/raw/summary/windows | PASS | None |
| slot 188 package/control/progress/raw/summary/windows | PASS | None |
| slot 189 package/control/progress/raw/summary/windows | PASS | None |
| slot 190 package/control/progress/raw/summary/windows | PASS | None |
| slot 191 package/control/progress/raw/summary/windows | PASS | None |
| slot 192 package/control/progress/raw/summary/windows | PASS | None |
| slot 193 package/control/progress/raw/summary/windows | PASS | None |
| slot 194 package/control/progress/raw/summary/windows | PASS | None |
| slot 195 package/control/progress/raw/summary/windows | PASS | None |
| slot 196 package/control/progress/raw/summary/windows | PASS | None |
| slot 197 package/control/progress/raw/summary/windows | PASS | None |
| slot 198 package/control/progress/raw/summary/windows | PASS | None |
| slot 199 package/control/progress/raw/summary/windows | PASS | None |
| slot 200 package/control/progress/raw/summary/windows | PASS | None |
| slot 201 package/control/progress/raw/summary/windows | PASS | None |
| slot 202 package/control/progress/raw/summary/windows | PASS | None |
| slot 203 package/control/progress/raw/summary/windows | PASS | None |
| slot 204 package/control/progress/raw/summary/windows | PASS | None |
| slot 205 package/control/progress/raw/summary/windows | PASS | None |
| slot 206 package/control/progress/raw/summary/windows | PASS | None |
| slot 207 package/control/progress/raw/summary/windows | PASS | None |
| slot 208 package/control/progress/raw/summary/windows | PASS | None |
| slot 209 package/control/progress/raw/summary/windows | PASS | None |
| slot 210 package/control/progress/raw/summary/windows | PASS | None |
| slot 211 package/control/progress/raw/summary/windows | PASS | None |
| slot 212 package/control/progress/raw/summary/windows | PASS | None |
| slot 213 package/control/progress/raw/summary/windows | PASS | None |
| slot 214 package/control/progress/raw/summary/windows | PASS | None |
| slot 215 package/control/progress/raw/summary/windows | PASS | None |
| slot 216 package/control/progress/raw/summary/windows | PASS | None |
| slot 217 package/control/progress/raw/summary/windows | PASS | None |
| slot 218 package/control/progress/raw/summary/windows | PASS | None |
| slot 219 package/control/progress/raw/summary/windows | PASS | None |
| cell 00 exact diagnostic analysis reconstruction | PASS | None |
| cell 01 exact diagnostic analysis reconstruction | PASS | None |
| cell 02 exact diagnostic analysis reconstruction | PASS | None |
| cell 03 exact diagnostic analysis reconstruction | PASS | None |
| cell 04 exact diagnostic analysis reconstruction | PASS | None |
| cell 05 exact diagnostic analysis reconstruction | PASS | None |
| cell 06 exact diagnostic analysis reconstruction | PASS | None |
| cell 07 exact diagnostic analysis reconstruction | PASS | None |
| cell 08 exact diagnostic analysis reconstruction | PASS | None |
| cell 09 exact diagnostic analysis reconstruction | PASS | None |
| cell 10 exact diagnostic analysis reconstruction | PASS | None |
| cell 11 exact diagnostic analysis reconstruction | PASS | None |
| cell 12 exact diagnostic analysis reconstruction | PASS | None |
| cell 13 exact diagnostic analysis reconstruction | PASS | None |
| cell 14 exact diagnostic analysis reconstruction | PASS | None |
| cell 15 exact diagnostic analysis reconstruction | PASS | None |
| cell 16 exact diagnostic analysis reconstruction | PASS | None |
| cell 17 exact diagnostic analysis reconstruction | PASS | None |
| cell 18 exact diagnostic analysis reconstruction | PASS | None |
| cell 19 exact diagnostic analysis reconstruction | PASS | None |
| cell 20 exact diagnostic analysis reconstruction | PASS | None |
| cell 21 exact diagnostic analysis reconstruction | PASS | None |
| exact canonical campaign analysis and claim firewall | PASS | {'relative_path': 'results/local/ex2-s7-3060ti-e2-8f1565f-stage6-analysis/campaign-analysis.json', 'sha256': '700859aa237c0eca55cf59ede7392b00f8a1391e636fc873b8f52f88797b8bb8', 'size_bytes': 1931} |
| E2 candidate/final/precreation/authorization anchors | PASS | E2 candidate bytes and frozen identity independently checked; prelaunch PASS is not sole proof |
| original one-launch scheduled task and native terminal reconciliation | PASS | one retained task dispatch and one native creation; sequential 220 child records, no retry within retained evidence |
| retained execution metadata probe and restoration context | PASS | retained metadata-only same-GPU/OBS-off context and AC sleep restoration |
| shared Phase-A candidate/final/stability/deployment artifact authority | PASS | shared artifact build authority verified; historical E1 manifest not conflated with E2 |
| retained producer inventories independently anchored to actual bytes | PASS | {'verified': 999, 'historical_e1_excluded': 2} |
| summary mathematical known answer | PASS | None |
| sealed E2 path set unchanged | PASS | None |
| all original bytes, sizes, paths and mtimes unchanged | PASS | None |
| all shared historical preparation authority unchanged | PASS | None |
| audit outputs ignored and confined to fresh roots | PASS | None |

NO GPU WORK / NO CAMPAIGN LAUNCH / NO HISTORICAL EDITS

NO CURATION / NO BENCHMARK INTERPRETATION / NO COMMIT / NO PUSH
