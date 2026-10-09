"""Independent Python stdlib verifier. Does not import generator/project serializer or execute supervisor."""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from gpu_identity import capture

ROOT = Path('C:/Users/rolan/src/ComputeLab-Stage7-3060Ti')
PREP = ROOT / 'results/local/ex2-s7-preparation-20261009-h2'
ID = 'ex2-s7-3060ti-e1-8f1565f'
MACHINE = 'ex2-s7-3060ti-machine'
SOURCE = '8f1565f7fb92e2184f4d2382aabf8fa7d00e1274'
GPU = '99a2369e-ca50-aac6-5c2c-fcaa44625084'
CANDIDATE = ROOT / f'results/tmp/{ID}-prep/candidate-manifest.json'
FINAL = ROOT / f'results/local/{ID}-stage6-manifest.json'
PATHS = {'child': 'out/build/x64-release/src/app/ComputeLabEx2Stage6.exe', 'supervisor': 'out/build/x64-release/src/app/ComputeLabEx2Stage6Supervisor.exe', **{key: f'out/build/x64-release/src/vulkan/Ex2{variant}.comp.spv' for key, variant in [('a1','A1'),('a2','A2'),('b1','B1'),('b2','B2'),('c','C'),('d1','D1')]}}
CELLS = [('A1',256),('A1',262144),('A1',16777216),('A2',262144),('A2',16777216),('B1',262144,'structured-v1'),('B1',262144,'shuffled-v1'),('B1',16777216,'shuffled-v1'),('B2',262144,'structured-v1'),('B2',262144,'shuffled-v1'),('B2',16777216,'shuffled-v1'),('C',1048576,1048576),('C',1048576,32768),('C',1048576,64),('D1',262144,16),('D1',1048576,64),('E1',1024,'host-to-device'),('E1',1048576,'host-to-device'),('E1',67108864,'host-to-device'),('E2',1024,'device-to-host'),('E2',1048576,'device-to-host'),('E2',67108864,'device-to-host')]

def require(test, reason):
    if not test:
        raise ValueError(reason)

def safe(path, regular=False):
    require(path.is_absolute() and path.is_relative_to(ROOT), f'Outside root: {path}')
    for part in [path, *path.parents]:
        if os.path.lexists(part):
            require(not getattr(part.lstat(), 'st_file_attributes', 0) & 0x400, f'Reparse alias: {part}')
    if regular:
        require(path.is_file(), f'Nonregular: {path}')

def pairs(items):
    result = {}
    for key, value in items:
        require(key not in result, f'Duplicate key: {key}')
        result[key] = value
    return result

def read(path):
    safe(path, True)
    return json.loads(path.read_bytes().decode('utf-8', errors='strict'), object_pairs_hook=pairs, parse_constant=lambda value: (_ for _ in ()).throw(ValueError(value)))

def canon(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=False, allow_nan=False).encode('utf-8')

def equal(actual, expected, name):
    require(type(actual) is type(expected), f'{name}: type')
    if isinstance(expected, dict):
        require(actual.keys() == expected.keys(), f'{name}: keys')
        for key in expected:
            equal(actual[key], expected[key], name + '.' + key)
    elif isinstance(expected, list):
        require(len(actual) == len(expected), f'{name}: count')
        for i, value in enumerate(expected):
            equal(actual[i], value, f'{name}[{i}]')
    else:
        require(actual == expected, f'{name}: value')

def git(*args):
    return subprocess.run(['git', *args], cwd=ROOT, check=True, capture_output=True, text=True).stdout.strip()

def digest(path):
    safe(path, True)
    return hashlib.sha256(path.read_bytes()).hexdigest()

def verify(path, phase):
    require(Path.cwd() == ROOT, 'Wrong execution worktree')
    safe(ROOT)
    require(git('rev-parse', 'HEAD') == SOURCE and not git('branch', '--show-current'), 'HEAD/detached gate')
    require(not git('status', '--porcelain=v1', '--untracked-files=all'), 'Dirty source')
    require(git('rev-parse', '--git-common-dir').replace('\\','/') == 'C:/Users/rolan/Desktop/CmptLab/ComputeLab/.git', 'Common directory changed')
    for value, maximum in [(ID,87),(MACHINE,96)]:
        require(len(value) <= maximum and re.fullmatch('[A-Za-z0-9_-]+', value), 'Identifier constraint')
    safe(path, True)
    raw, manifest = path.read_bytes(), read(path)
    require(len(raw) <= 1048576 and not raw.startswith(b'\xef\xbb\xbf') and b'\r' not in raw and raw.count(b'\n') == 1 and raw.endswith(b'\n'), 'Canonical byte envelope')
    require(raw == canon(manifest) + b'\n', 'Noncanonical bytes')
    payload = {key:value for key,value in manifest.items() if key != 'manifest_sha256'}
    semantic = hashlib.sha256(canon(payload)).hexdigest()
    physical = hashlib.sha256(raw).hexdigest()
    freeze = read(PREP / 'artifact-freeze.json')
    require(set(freeze) == {'record_version','record_type','source_revision','git_clean','detached_head','build_preset','build_type','machine_id','manifest_id','protocol_version','evidence_schema_version','evidence_kind','instrument_mode','cuda_device_ordinal','vulkan_physical_device_index','cuda_uuid','vulkan_uuid','artifacts','provenance','debug_targeted_pass_count','release_direct_pass_count','release_supervisor_pass_count','canonical_debug_suite','debug_build_log_limitation'}, 'Freeze extra/missing fields')
    artifacts = {}
    for key, relative in PATHS.items():
        live = ROOT / relative
        artifacts[key] = dict(path=relative, size_bytes=live.stat().st_size, sha256=digest(live))
    equal(freeze['artifacts'], artifacts, 'Eight actual artifacts')
    for key, expected in dict(record_version=1, record_type='ex2-stage7-deployment-artifact-freeze', source_revision=SOURCE, git_clean=True, detached_head=True, build_preset='x64-release', build_type='Release', machine_id=MACHINE, manifest_id=ID, protocol_version='1.4', evidence_schema_version=2, evidence_kind='diagnostic', instrument_mode='H', cuda_device_ordinal=0, vulkan_physical_device_index=0, cuda_uuid=GPU, vulkan_uuid=GPU, debug_targeted_pass_count=3, release_direct_pass_count=4, release_supervisor_pass_count=4).items():
        equal(freeze[key], expected, 'Freeze.' + key)
    require('INCOMPLETE' in freeze['canonical_debug_suite'] and 'NOT RERUN' in freeze['canonical_debug_suite'], 'Waiver concealed')
    require(freeze['provenance']['inventory_sha256'] == digest(PREP / 'inventory.log'), 'Inventory drift')
    expected = dict(manifest_version=1, manifest_type='ex2-stage6-diagnostic', manifest_id=ID, manifest_sha256=semantic, protocol_version='1.4', evidence_schema_version=2, evidence_kind='diagnostic', instrument_mode='H', machine_id=MACHINE, expected_source_revision=SOURCE, expected_git_dirty=False, child_executable_path=PATHS['child'], expected_child_executable_sha256=artifacts['child']['sha256'], expected_supervisor_executable_sha256=artifacts['supervisor']['sha256'], expected_vulkan_shader_sha256={key:artifacts[key]['sha256'] for key in ['a1','a2','b1','b2','c','d1']}, expected_gpu_uuid=GPU, cuda_device_ordinal=0, vulkan_physical_device_index=0, operation_timeout_ms=60000, child_timeout_ms=1200000, campaign_timeout_ms=86400000, continuation_policy='resolved-only-no-retry', declared_cell_group_count=22, declared_child_count=220, groups=[])
    schedule = []
    orders = [('cuda','vulkan'),('vulkan','cuda'),('cuda','vulkan'),('vulkan','cuda'),('cuda','vulkan')]
    for cell, workload in enumerate(CELLS):
        children = []
        for block, order in enumerate(orders):
            for position, backend in enumerate(order):
                sequence = cell * 10 + block * 2 + position
                session = f'{ID}-slot-{sequence:03d}'
                children.append(dict(sequence_index=sequence, session_id=session))
                schedule.append(dict(sequence_index=sequence, session_id=session, cell_index=cell, workload=list(workload), backend=backend, block_index=block, process_index=block, order_slot=position, sample_indices=[0,99], planned_sample_count=100, warmup_count=0))
        expected['groups'].append(dict(cell_index=cell, declared_child_count=10, children=children))
    equal(manifest, expected, 'Strict manifest')
    require(len(schedule) == 220 and len({s['session_id'].lower() for s in schedule}) == 220 and sum(s['backend']=='cuda' for s in schedule)==110, 'Schedule uniqueness/count')
    cache = (ROOT / 'out/build/x64-release/CMakeCache.txt').read_text()
    require('CMAKE_BUILD_TYPE:STRING=Release' in cache and 'CMAKE_TOOLCHAIN_FILE:FILEPATH=C:/Program Files/Microsoft Visual Studio/18/Community/VC/vcpkg/scripts/buildsystems/vcpkg.cmake' in cache, 'Release/toolchain mismatch')
    ninja = (ROOT / 'out/build/x64-release/build.ninja').read_text()
    require('-arch=native' in ninja and 'sm_86.cubin' in (PREP / 'release-cuda-elf.log').read_text(), 'Native architecture gate')
    buildlog = (PREP / 'release-build.log').read_text()
    for shader in ['Ex2A1','Ex2A2','Ex2B1','Ex2B2','Ex2C','Ex2D1','Ex1Transform']:
        require(f'Generating {shader}.comp.spv' in buildlog, 'Shader compilation missing')
    for name in ['debug-build','debug-smoke','release-build','direct-smoke','supervisor-smoke']:
        require((PREP / f'{name}-exit.txt').read_text().strip() == '0', f'Exit gate {name}')
    last = (PREP / 'debug-last-test.log').read_text()
    require(len(re.findall(r'Test Passed\.', last)) == 3 and '99a2369eca50aac65c2cfcaa44625084' in last, 'Debug evidence count/UUID')
    for case in ['CUDA','Vulkan','GpuIdentity']:
        require(f'Test: ComputeLabSmoke.{case}' in last, 'Debug inventory')
    for name, suite_name in [('direct','Ex2Stage6ReleaseSmoke'),('supervisor','Ex2Stage6SupervisorReleaseSmoke')]:
        suite = read(PREP / f'{name}-smoke.json')
        require(suite['tests'] == 4 and all(suite[k] == 0 for k in ['failures','disabled','errors']), 'Release count gate')
        require(len(suite['testsuites']) == 1 and suite['testsuites'][0]['name'] == suite_name, 'Release suite gate')
        cases = suite['testsuites'][0]['testsuite']
        require([case['name'] for case in cases] == ['A1Cuda','A1Vulkan','E1Cuda','E1Vulkan'] and all(case['status']=='RUN' and case['result']=='COMPLETED' and 'failures' not in case for case in cases), 'Release exact cases')
    gpu = capture()
    require(gpu['cuda_uuid'] == gpu['vulkan_uuid'] == GPU and gpu['cuda_device_ordinal'] == gpu['vulkan_physical_device_index'] == 0, 'Live API selector/UUID mismatch')
    equal(gpu, read(PREP / 'gpu-freeze.json'), 'API identity drift')
    absent = [ROOT / f"results/local/{slot['session_id']}{suffix}" for slot in schedule for suffix in ['', '.incomplete', '.failure.json']]
    absent += [ROOT / f'results/local/{ID}-stage6-control.json{suffix}' for suffix in ['', '.incomplete', '.incomplete.tmp']]
    absent += [ROOT / f'results/local/{ID}-stage6-analysis', ROOT / f'results/tmp/{ID}-execution']
    require(len(absent) == len({str(p).lower() for p in absent}) == 665, 'Namespace count/alias')
    for destination in absent:
        safe(destination)
        require(not os.path.lexists(destination), f'Namespace collision: {destination}')
    require(not list((ROOT / 'results/local').glob('s6-i*-smoke-*')), 'Fixture cleanup')
    if phase == 'candidate':
        require(path == CANDIDATE and not os.path.lexists(FINAL), 'Final manifest already exists')
    else:
        require(path == FINAL and raw == CANDIDATE.read_bytes(), 'Publication bytes differ')
    if phase == 'stability':
        prior = read(PREP / 'final-verification.json')
        require(prior['verification_pass'] is True and prior['physical_sha256'] == physical and prior['semantic_sha256'] == semantic, 'Stability identity differs')
    return dict(verification_pass=True, phase=phase, invocation=sys.argv, verified_utc=datetime.now(timezone.utc).isoformat(), execution_worktree=str(ROOT), source_revision=SOURCE, git_clean=True, detached_head=True, manifest_id=ID, machine_id=MACHINE, semantic_sha256=semantic, physical_sha256=physical, artifacts=artifacts, gpu=gpu, cell_count=22, child_count=220, planned_operations=22000, schedule=schedule, namespace_clear=True, absent_paths_checked=665, absent_paths=[str(p.relative_to(ROOT)) for p in absent], final_manifest_absent=(phase=='candidate'), canonical_debug_suite='INCOMPLETE; NOT RERUN', targeted_debug_pass=3, direct_release_pass=4, supervised_release_pass=4, production_launch_authorized=False, production_outputs_present=False)

if __name__ == '__main__':
    manifest, phase, report = sys.argv[1:]
    require(phase in ['candidate','final','stability'], 'Unknown phase')
    receipt = Path(report)
    safe(receipt)
    with receipt.open('x', encoding='utf-8', newline='\n') as out:
        try:
            result = verify(Path(manifest), phase)
        except Exception as error:
            json.dump(dict(verification_pass=False, phase=phase, invocation=sys.argv, error=str(error)), out, indent=2)
            out.write('\n')
            raise
        json.dump(result, out, sort_keys=True, indent=2)
        out.write('\n')
    print(f'{phase}: PASS semantic={result["semantic_sha256"]} physical={result["physical_sha256"]} namespaces=665')
