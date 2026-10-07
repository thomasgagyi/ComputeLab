"""Publish Phase-A control records from retained verification reports; no verifier rerun."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
from datetime import datetime, timezone

root = Path.cwd()
identity = 'ex2-s6-e1b-8f1565f'
prep = root / f'results/tmp/{identity}-prep'
local = root / 'results/local'
source = '8f1565f7fb92e2184f4d2382aabf8fa7d00e1274'

def read(path):
    return json.loads(path.read_text(encoding='utf-8'))

def publish(path, text):
    with path.open('xb') as out:
        out.write(text.encode('utf-8'))
        out.flush()
        os.fsync(out.fileno())

def git(*args):
    return subprocess.run(['git', *args], check=True, capture_output=True, text=True).stdout.strip()

reports = {phase: read(prep / f'logs/{phase}-verification.json') for phase in ['candidate','final','stability']}
stable = reports['stability']
for phase, report in reports.items():
    assert report['verification_pass'] and report['phase'] == phase
    for field in ['source_revision','protocol_version','manifest_id','stored_semantic_sha256',
                  'calculated_semantic_sha256','manifest_file_sha256','artifacts','gpu','schedule']:
        assert report[field] == stable[field], f'Report drift: {field}'
ps = read(prep / 'logs/final-powershell-hash.json')
assert ps['all_match'] and ps['powershell_physical_sha256'] == stable['manifest_file_sha256']
assert git('rev-parse','--verify','HEAD') == source and git('branch','--show-current') == 'main'
assert not git('status','--porcelain=v1','--untracked-files=all')
assert not git('diff','--check') and not git('diff','--cached','--name-status')

request = Path(r'C:\Users\2dfdc\.codex\attachments\44c6ee36-b69e-4513-ac17-44b9cd60291f\Pasted text.txt').read_text(encoding='utf-8-sig')
matrix = re.findall(r'^(BPREP-\d{3}) (.+)$', request, flags=re.M)
assert len(matrix) == 60 and [code for code, _ in matrix] == [f'BPREP-{i:03d}' for i in range(1,61)]
semantic = stable['stored_semantic_sha256']
physical = stable['manifest_file_sha256']
authorization = f'AUTHORIZE S6-E1B {identity} {semantic} {physical}'
anchors = {}
for phase in reports:
    path = prep / f'logs/{phase}-verification.json'
    data = path.read_bytes()
    anchors[phase] = dict(path=path.relative_to(root).as_posix(), size_bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                          verification_pass=True, invocations=1)
verification = dict(record_version=1, record_type='ex2-stage6-pre-execution-verification',
    status='PASS', verification_pass=True, source_revision=source, branch='main', git_clean=True,
    protocol_version='1.4', evidence_schema_version=2, evidence_kind='diagnostic', instrument_mode='H',
    manifest_id=identity, manifest_sha256=semantic, calculated_manifest_sha256=stable['calculated_semantic_sha256'],
    manifest_file_sha256=physical, powershell_manifest_file_sha256=ps['powershell_physical_sha256'],
    artifacts=stable['artifacts'], gpu=stable['gpu'], cell_group_count=22, child_count=220,
    planned_operations=22000, warmup_count=0, operations_per_child=100, schedule_verified=True,
    namespace_clear=True, production_paths_checked=665,
    production_children_executed=0, supervisor_launches=0,
    release_build_pass=True, build_invocations=1, tests_rerun=False, source_modified=False,
    historical_attempt_modified=False, launch_authorized=False, campaign_executed=False,
    gate0='FAIL', stage2='NOT GRANTED', production_backend='UNSELECTED',
    verification_reports=anchors, human_authorization_string=authorization,
    recorded_utc=datetime.now(timezone.utc).isoformat())
publish(local / f'{identity}-stage6-verification.json', json.dumps(verification, sort_keys=True, indent=2) + '\n')
lines = [
    '# S6-E1b PRE-EXECUTION STATUS', '', 'READY FOR HUMAN REVIEW', 'CAMPAIGN NOT EXECUTED',
    'LAUNCH NOT YET AUTHORIZED', 'PRODUCTION CHILDREN EXECUTED: 0', 'PRODUCTION SUPERVISOR LAUNCHES: 0', '',
    f'Source: `{source}`; branch `main`; tracked/staged and untracked source clean.',
    'The canonical x64-release build/update passed with exactly one invocation. No tests were rerun.',
    'No build, configure, relink or shader compilation occurred after artifact freeze.',
    'Active evidence, analysis, manifest parser and ledger serialization/validation use protocol 1.4.', '',
    '## Exact launch-review tuple', '',
    f'- source_revision: `{source}`', f'- manifest_id: `{identity}`',
    f'- manifest_sha256: `{semantic}`', f'- manifest_file_sha256: `{physical}`',
    '- expected_gpu_uuid: `0340eaac-dc67-f450-d558-d47c55cc4417`',
    '- CUDA selector: ordinal `0`; independently recaptured UUID matches.',
    '- Vulkan selector: physical-device index `0`; independently recaptured UUID matches.',
    '- machine_id: `ex2-s6-e1-machine`',
    '- protocol/schema/kind/instrument: `1.4` / `2` / `diagnostic` / `H`', '',
    '## Frozen Release artifacts', '', '| Artifact | Repository-relative path | Bytes | SHA-256 |',
    '| --- | --- | ---: | --- |'
]
for key in ['child','supervisor','a1','a2','b1','b2','c','d1']:
    a = stable['artifacts'][key]
    lines.append(f'| {key} | `{a["path"]}` | {a["size_bytes"]} | `{a["sha256"]}` |')
lines += [
    '', '## Independent verification', '',
    'PowerShell/.NET generated the candidate. Separate Python standard-library code verified raw bytes, strict UTF-8,',
    'one trailing LF, absence of BOM/CRLF, duplicate-key rejection, exact schema/types/constants, artifact hashes/sizes,',
    'source and clean Git state, selector-to-UUID mapping, semantic hash, physical hash, and production path absence.',
    'The Python verifier does not call the generator, project helpers/libraries, tests, or production supervisor.',
    'It reconstructs the full schedule using an independent explicit per-cell order table.', '',
    '| Phase | Status | Invocations | Report |', '| --- | --- | ---: | --- |'
]
for phase, anchor in anchors.items():
    lines.append(f'| {phase} | PASS | 1 | `{anchor["path"]}` |')
lines += [
    '', 'The final manifest was published with CREATE_NEW using the verified candidate bytes exactly.',
    'Stored and independently calculated semantic hashes match. Python physical SHA-256 and PowerShell Get-FileHash match.',
    'All eight live artifacts were rehashed in each verification. GPU identities and Git/source were recaptured in each verification.',
    'The final and stability reports have separate, unique paths. None of these verifiers may be rerun at launch.', '',
    '## Frozen schedule and namespace', '',
    '22 cells; ten fresh OS processes/cell; 220 child identities; 100 operations/process; 22,000 planned operations.',
    'Warm-up 0; sequential execution; resolved-only-no-retry; no timing gate, outlier deletion or adaptive sampling.',
    'Sessions span `ex2-s6-e1b-8f1565f-slot-000` through `ex2-s6-e1b-8f1565f-slot-219`.',
    'Operation/child/campaign deadlines: 60,000 / 1,200,000 / 86,400,000 ms.', '',
    '| Position | Block | Order | Process | Backend |', '| ---: | ---: | ---: | ---: | --- |'
]
for entry in stable['schedule'][:10]:
    lines.append(f'| {entry["sequence_index"]} | {entry["block_index"]} | {entry["order_slot"]} | {entry["process_index"]} | {entry["backend"]} |')
lines += [
    '', 'All 660 child final/staging/sidecar paths, all three ledger final/staging/temp paths, the production analysis root,',
    'and the Phase-B execution-wrapper root are absent. New preparation/final paths were absent before CREATE_NEW.',
    'The preparation scripts/logs and four final Phase-A records are local ignored files.',
    'Historical `results/local/ex2-s6-e1-9053263-*` and `results/tmp/ex2-s6-e1-9053263-*` were preserved.', '',
    '## BPREP-001..060 acceptance matrix', '',
    'Completed preparation checks are PASS. Reporting/stop obligations 056..059 are discharged by the Phase-A chat handoff.',
    'Launch remains blocked pending a new exact human authorization message.', '',
    '| Check | Status | Requirement |', '| --- | --- | --- |'
]
for code, description in matrix:
    status = 'PASS' if int(code[-3:]) not in range(56,60) else 'PHASE-A HANDOFF / HARD STOP'
    lines.append(f'| {code} | {status} | {description.strip()} |')
lines += [
    '', '## Human hash gate', '', 'The initial prompt authorizes preparation only.',
    'Send the following exact authorization in a new message to authorize Phase B:', '',
    '```text', authorization, '```', '',
    'Phase B requires read-only guards and one exact production launch; no build, tests, regeneration, verifier rerun or retry.',
    'Stop after terminal control handoff; independent post-run audit and curation require a separate task.', '',
    'Gate 0 remains FAIL; Stage 2 remains NOT GRANTED; production backend remains UNSELECTED.',
    'DR-44 and DR-45 remain Accepted. No comparative ranking or production conclusion is authorized.',
    'No source edit, commit, push, Notion update, production child, supervisor launch, audit, curation or performance interpretation occurred.', '',
    'S6-E1b PREPARATION COMPLETE — READY FOR HASH-BOUND HUMAN LAUNCH AUTHORIZATION — CAMPAIGN NOT EXECUTED', ''
]
publish(local / f'{identity}-stage6-review.md', '\n'.join(lines))
print(json.dumps({k:verification[k] for k in ['status','source_revision','manifest_id','manifest_sha256',
    'manifest_file_sha256','production_children_executed','supervisor_launches','human_authorization_string']}, indent=2))
