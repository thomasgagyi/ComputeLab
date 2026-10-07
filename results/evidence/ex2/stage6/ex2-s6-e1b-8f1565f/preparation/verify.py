"""Independent standard-library verifier. Never calls generator/project/supervisor."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from datetime import datetime, timezone
from gpu_identity import capture

ROOT = Path.cwd()
ID = 'ex2-s6-e1b-8f1565f'
SOURCE = '8f1565f7fb92e2184f4d2382aabf8fa7d00e1274'
GPU = '0340eaac-dc67-f450-d558-d47c55cc4417'
PATHS = {
    'child': 'out/build/x64-release/src/app/ComputeLabEx2Stage6.exe',
    'supervisor': 'out/build/x64-release/src/app/ComputeLabEx2Stage6Supervisor.exe',
    **{k: f'out/build/x64-release/src/vulkan/Ex2{n}.comp.spv'
       for k, n in [('a1','A1'), ('a2','A2'), ('b1','B1'), ('b2','B2'), ('c','C'), ('d1','D1')]}
}

def require(condition, message):
    if not condition:
        raise ValueError(message)

def safe(path, regular=False):
    require(path.is_absolute() and path.is_relative_to(ROOT), 'Path outside repository')
    for p in [path, *path.parents]:
        if p.exists() or p.is_symlink():
            require(not getattr(p.lstat(), 'st_file_attributes', 0) & 0x400, f'Reparse point: {p}')
        if p == ROOT:
            break
    if regular:
        require(path.is_file(), f'Not a regular file: {path}')

def pairs(items):
    result = {}
    for key, value in items:
        require(key not in result, f'Duplicate key: {key}')
        result[key] = value
    return result

def read_json(path):
    safe(path, True)
    return json.loads(path.read_bytes().decode('utf-8'), object_pairs_hook=pairs)

def canonical(value):
    return json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(',', ':'), allow_nan=False).encode('utf-8')

def equal(actual, expected, label):
    require(type(actual) is type(expected), f'{label} type differs')
    if isinstance(expected, dict):
        require(actual.keys() == expected.keys(), f'{label} schema differs')
        for key in expected:
            equal(actual[key], expected[key], f'{label}.{key}')
    elif isinstance(expected, list):
        require(len(actual) == len(expected), f'{label} count differs')
        for i, value in enumerate(expected):
            equal(actual[i], value, f'{label}[{i}]')
    else:
        require(actual == expected, f'{label} differs')

def git(*args):
    return subprocess.run(['git', *args], cwd=ROOT, check=True, capture_output=True, text=True).stdout.strip()

def verify(path, phase):
    safe(path, True)
    raw = path.read_bytes()
    require(not raw.startswith(b'\xef\xbb\xbf') and b'\r' not in raw, 'BOM/CR bytes')
    require(raw.endswith(b'\n') and raw.count(b'\n') == 1, 'Exactly one trailing LF required')
    manifest = json.loads(raw.decode('utf-8'), object_pairs_hook=pairs,
                          parse_constant=lambda x: (_ for _ in ()).throw(ValueError(x)))
    require(raw == canonical(manifest) + b'\n', 'Physical bytes not canonical')
    semantic = manifest.get('manifest_sha256')
    require(isinstance(semantic, str) and re.fullmatch('[0-9a-f]{64}', semantic), 'Semantic SHA shape')
    m0 = {k:v for k,v in manifest.items() if k != 'manifest_sha256'}
    calculated = hashlib.sha256(canonical(m0)).hexdigest()
    require(semantic == calculated, 'Semantic SHA mismatch')
    physical = hashlib.sha256(raw).hexdigest()
    freeze = read_json(ROOT / f'results/local/{ID}-stage6-artifact-freeze.json')
    expected_freeze_fields = {'record_version','record_type','source_revision','git_clean','build_preset','build_type',
        'machine_id','expected_gpu_uuid','cuda_device_ordinal','cuda_uuid','vulkan_physical_device_index',
        'vulkan_uuid','artifacts','protocol_version','evidence_schema_version','evidence_kind','instrument_mode',
        'release_build_pass','tests_rerun'}
    require(freeze.keys() == expected_freeze_fields, 'Freeze schema')
    artifacts = {}
    for key, relative in PATHS.items():
        live = ROOT / relative
        safe(live, True)
        data = live.read_bytes()
        artifacts[key] = dict(path=relative, size_bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
    equal(freeze['artifacts'], artifacts, 'Live eight-artifact freeze')
    expected = dict(manifest_version=1, manifest_type='ex2-stage6-diagnostic', manifest_id=ID,
        manifest_sha256=calculated, protocol_version='1.4', evidence_schema_version=2,
        evidence_kind='diagnostic', instrument_mode='H', machine_id='ex2-s6-e1-machine',
        expected_source_revision=SOURCE, expected_git_dirty=False,
        child_executable_path=PATHS['child'], expected_child_executable_sha256=artifacts['child']['sha256'],
        expected_supervisor_executable_sha256=artifacts['supervisor']['sha256'],
        expected_vulkan_shader_sha256={k:artifacts[k]['sha256'] for k in ['a1','a2','b1','b2','c','d1']},
        expected_gpu_uuid=GPU, cuda_device_ordinal=0, vulkan_physical_device_index=0,
        operation_timeout_ms=60000, child_timeout_ms=1200000, campaign_timeout_ms=86400000,
        continuation_policy='resolved-only-no-retry', declared_cell_group_count=22, declared_child_count=220,
        groups=[dict(cell_index=g, declared_child_count=10,
                     children=[dict(sequence_index=g*10+p, session_id=f'{ID}-slot-{g*10+p:03d}')
                               for p in range(10)]) for g in range(22)])
    equal(manifest, expected, 'Manifest')
    expected_freeze = dict(record_version=1, record_type='ex2-stage6-artifact-freeze',
        source_revision=SOURCE, git_clean=True, build_preset='x64-release', build_type='Release',
        machine_id='ex2-s6-e1-machine', expected_gpu_uuid=GPU, cuda_device_ordinal=0, cuda_uuid=GPU,
        vulkan_physical_device_index=0, vulkan_uuid=GPU, artifacts=artifacts, protocol_version='1.4',
        evidence_schema_version=2, evidence_kind='diagnostic', instrument_mode='H',
        release_build_pass=True, tests_rerun=False)
    equal(freeze, expected_freeze, 'Freeze')
    sessions = [child['session_id'] for group in manifest['groups'] for child in group['children']]
    require(len(sessions) == len(set(sessions)) == 220, 'Session uniqueness')
    order = [(0,0,0,'cuda'), (0,1,0,'vulkan'), (1,0,1,'vulkan'), (1,1,1,'cuda'),
             (2,0,2,'cuda'), (2,1,2,'vulkan'), (3,0,3,'vulkan'), (3,1,3,'cuda'),
             (4,0,4,'cuda'), (4,1,4,'vulkan')]
    schedule = []
    for group in manifest['groups']:
        for child, (block, order_slot, process, backend) in zip(group['children'], order, strict=True):
            schedule.append(dict(**child, cell_index=group['cell_index'], block_index=block,
                                 order_slot=order_slot, process_index=process, backend=backend))
    require(sum(s['backend']=='cuda' for s in schedule)==110, 'CUDA count')
    require(sum(s['backend']=='vulkan' for s in schedule)==110, 'Vulkan count')
    require(git('rev-parse','--verify','HEAD') == SOURCE, 'Source mismatch')
    require(git('branch','--show-current') == 'main', 'Branch mismatch')
    require(not git('status','--porcelain=v1','--untracked-files=all'), 'Source dirty')
    require(not git('diff','--check') and not git('diff','--cached','--name-status'), 'Git diff/index')
    gpu = capture()
    absent = [ROOT / f'results/local/{sid}{suffix}' for sid in sessions
              for suffix in ('', '.incomplete', '.failure.json')]
    absent += [ROOT / f'results/local/{ID}-stage6-control.json{suffix}'
               for suffix in ('', '.incomplete', '.incomplete.tmp')]
    absent += [ROOT / f'results/local/{ID}-stage6-analysis', ROOT / f'results/tmp/{ID}-execution',
               ROOT / f'results/local/{ID}-stage6-verification.json', ROOT / f'results/local/{ID}-stage6-review.md']
    final = ROOT / f'results/local/{ID}-stage6-manifest.json'
    if phase == 'candidate':
        absent.append(final)
    else:
        require(final.read_bytes() == (ROOT / f'results/tmp/{ID}-prep/candidate-manifest.json').read_bytes(),
                'Promotion bytes differ')
    for p in absent:
        safe(p)
        require(not os.path.lexists(p), f'Namespace collision: {p}')
    return dict(verification_pass=True, phase=phase, source_revision=SOURCE, branch='main', git_clean=True,
        protocol_version='1.4', manifest_id=ID, stored_semantic_sha256=semantic,
        calculated_semantic_sha256=calculated, manifest_file_sha256=physical, artifacts=artifacts,
        gpu=gpu, cell_group_count=22, child_count=220, planned_operations=22000,
        schedule=schedule, schedule_verified=True, namespace_clear=True, absent_paths_checked=len(absent),
        production_children_executed=0, supervisor_launches=0,
        verified_utc=datetime.now(timezone.utc).isoformat())

if __name__ == '__main__':
    manifest_path, phase, report_path = sys.argv[1:]
    require(phase in ('candidate','final','stability'), 'Unknown phase')
    report = ROOT / report_path
    safe(report)
    # Reserve report exactly once before inspection; failures remain retained.
    with report.open('x', encoding='utf-8', newline='\n') as output:
        try:
            result = verify(ROOT / manifest_path, phase)
        except Exception as error:
            json.dump(dict(verification_pass=False, phase=phase, error=str(error)), output, indent=2)
            output.write('\n')
            raise
        json.dump(result, output, sort_keys=True, indent=2)
        output.write('\n')
    print(f'{phase}: PASS semantic={result["stored_semantic_sha256"]} physical={result["manifest_file_sha256"]}')
