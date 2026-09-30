"""Read-only raw evidence audit; writes only derived files beside this script.

The committed Prompt-3 inspector remains the independent CPU semantic oracle.
This script additionally regenerates B/C logical inputs/expected digests from
the written formulas. It does not recover or claim to recover GPU output bytes.
No GPU launch, source edit, dependency install, or evidence rewrite occurs.
"""
import array
import csv
import hashlib
import io
import json
import pathlib
import re
import struct
import sys
from datetime import datetime

ROOT = pathlib.Path(__file__).resolve().parents[3]
OUT = pathlib.Path(__file__).resolve().parent
LOCAL = ROOT / 'results/local'
BASE = 'i7-stage4-343e0db'
SOURCE = '343e0dbcb28c46b6bbfa5309d72d6a5b0c5ffac7'
UUID = '0340eaac-dc67-f450-d558-d47c55cc4417'
SEED = 0x0123456789ABCDEF
MASK64 = (1 << 64) - 1
MASK32 = (1 << 32) - 1
SHADERS = ['c40c5ef608254d7763aa244d1ca6403605f750212a038ccf1913cff2bd61c63a',
           'bfda73b6abe562facaba4fa265b281144e2c8761517ffad71c8176184cf17f3f',
           '7eb4a7d40c2ea5aa133821290318b18b45c49f5284cb6ffc8ebda5f9b35e933c',
           '2761fe5fab23630ce7ed47a93afe74cd769d1b154fa13a33a49601ed02120a26',
           '7e4643308689eb49ee19a205ad126cf616e57f2c613b2e11007ad5a95446a4aa',
           '3d0787c7ce83802caca54cd9bc107d506949a509ba0ffe5f0604bf72ed9e7d61']
SHADER_MAP = [0]*3 + [1]*2 + [2]*3 + [3]*3 + [4]*3 + [5]*2 + [None]*6
VARIANTS = ['A1']*3 + ['A2']*2 + ['B1']*3 + ['B2']*3 + ['C']*3 + ['D1']*2 + ['E1']*3 + ['E2']*3
COUNTS = [256,262144,16777216,262144,16777216,262144,262144,16777216,
          262144,262144,16777216,1048576,1048576,1048576,262144,1048576,
          1024,1048576,67108864,1024,1048576,67108864]
INIT_HEADER = 'schema_version,run_id,experiment_id,backend,process_index,sequence_index,category,workload,variant,element_count,metric,duration_ns,observation'.split(',')
SAMPLE_HEADER = 'schema_version,run_id,experiment_id,comparison_condition_id,series_id,backend,workload,variant,seed,element_count,byte_count,index_pattern,counter_count,iteration_count,transfer_direction,execution_mode,instrument_mode,warmup_count,planned_sample_count,block_index,order_slot,process_index,sample_index,validation_passed,status,failure_phase,error_code,host_submission_ns,host_wait_ns,host_completion_ns,native_device_interval_ns'.split(',')
TIMINGS = SAMPLE_HEADER[-4:]
COMMON_KEYS = 'schema_version experiment_id run_id timestamp_utc git_commit git_dirty machine_id os_name os_version cpu_name system_memory_bytes gpu_name gpu_vendor gpu_device_id gpu_memory_bytes nvidia_driver_version cuda_toolkit_version cuda_runtime_version cuda_compute_capability vulkan_sdk_version vulkan_device_api_version compiler_name compiler_version cmake_version ninja_version configure_preset build_type validation_enabled diagnostic_instrumentation'.split()
ENV_KEYS = COMMON_KEYS + 'protocol_version evidence_kind backend instrument_mode warmup_count planned_sample_count comparison_condition_id series_id block_index process_index order_slot source_revision executable_sha256 shader_sha256 input_sha256 expected_output_sha256 gpu_uuid_identity workload variant generator_revision seed element_count byte_count index_pattern counter_count iteration_count transfer_direction execution_mode operation_boundary backend_native'.split()
NATIVE_KEYS = 'implementation stream_flags queue_family_index queue_flags queue_count timestamp_valid_bits timestamp_period_ns input_memory_flags output_memory_flags upload_memory_flags readback_memory_flags native_markers_enabled native_timing_method native_timing_resolution_ns native_timing_start_stage native_timing_stop_stage native_duration_envelope_ns'.split()

def require(value, message):
    if not value:
        raise ValueError(message)

def strict_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'duplicate JSON key: ' + key)
        result[key] = value
    return result

def reject_constant(value):
    raise ValueError('non-finite JSON token: ' + value)

def read_json(path):
    return json.loads(path.read_text(encoding='utf-8'), object_pairs_hook=strict_object,
                      parse_constant=reject_constant)

def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode('utf-8')

def digest(data):
    return hashlib.sha256(data).hexdigest()

def file_hash(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def equal(actual, expected, context):
    require(type(actual) is type(expected), context + ' type mismatch')
    if isinstance(expected, dict):
        require(set(actual) == set(expected), context + ' key mismatch')
        for key in expected:
            equal(actual[key], expected[key], context + '.' + key)
    elif isinstance(expected, list):
        require(len(actual) == len(expected), context + ' length mismatch')
        for index, item in enumerate(expected):
            equal(actual[index], item, context + '[' + str(index) + ']')
    else:
        require(actual == expected, context + ' value mismatch')

def keys(value, expected, context):
    require(type(value) is dict and set(value) == set(expected), context + ' exact keys')

def confined_regular(relative):
    require(type(relative) is str and relative.startswith('results/local/')
            and '\\' not in relative and ':' not in relative, 'artifact path syntax')
    path_parts = pathlib.PurePosixPath(relative)
    require(relative == path_parts.as_posix() and '..' not in path_parts.parts, 'normalized artifact path')
    path = ROOT / relative
    require(path.resolve().is_relative_to(LOCAL.resolve()), 'artifact confinement')
    for part in [path, *path.parents]:
        require(not part.is_symlink() and not part.is_junction(), 'reparse path')
        require(not (part.stat().st_file_attributes & 0x400), 'Windows reparse attributes')
        if part == ROOT:
            break
    require(path.is_file(), 'artifact not ordinary file')
    return path

def csv_rows(path, header):
    text = path.read_bytes().decode('utf-8')
    require(text.endswith('\r\n') and '\n' not in text.replace('\r\n', ''), 'CSV line endings')
    rows = list(csv.reader(io.StringIO(text, newline=''), strict=True))
    require(len(rows) == 2 and rows[0] == header and len(rows[1]) == len(header), 'CSV exact header/one row')
    return dict(zip(header, rows[1]))

def csv_value(value):
    if value is None:
        return ''
    if type(value) is bool:
        return str(value).lower()
    return str(value)

def finalizer(z):
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK64
    return z ^ (z >> 31)

def words_bytes(words):
    result = array.array('I', words)
    require(result.itemsize == 4, 'uint32 array width')
    if sys.byteorder != 'little':
        result.byteswap()
    return result.tobytes()

def composite(first_name, first, second_name, second):
    h = hashlib.sha256(b'ComputeLab/EX-2/logical-input/v1\0' + struct.pack('<I', 2))
    for name, payload in [(first_name, first), (second_name, second)]:
        encoded = name.encode('ascii')
        h.update(struct.pack('<I', len(encoded)) + encoded + struct.pack('<Q', len(payload)))
        h.update(payload)
    return h.hexdigest()

def parameters(index):
    variant = VARIANTS[index]
    workload = variant[0]
    return dict(workload=workload, variant=variant, seed=SEED,
                generator_revision='ex2-mix64-v1', element_count=None if workload == 'E' else COUNTS[index],
                byte_count=COUNTS[index] if workload == 'E' else None,
                index_pattern=('structured-v1' if index in [5,8] else 'shuffled-v1') if workload == 'B' else None,
                counter_count={11:1048576,12:32768,13:64}.get(index),
                iteration_count={14:16,15:64}.get(index),
                transfer_direction=('H2D' if variant == 'E1' else 'D2H') if workload == 'E' else None,
                execution_mode='prepared' if workload == 'E' else 'ordinary',
                operation_boundary={'C':'atomic-dispatch-completion','D':'ordinary-iteration-sequence-completion',
                                    'E':'prepared-single-copy-completion'}.get(workload,'single-dispatch-completion'))

def bc_reference(index, cache):
    p = parameters(index)
    n = p['element_count']
    if p['workload'] == 'C':
        active = p['counter_count']
        target_bytes = words_bytes(((8191*i+17) % n) % active for i in range(n))
        input_hash = composite('targets_words', target_bytes, 'initial_counters_words', bytes(4*n))
        expected_hash = digest(words_bytes([n//active]*active + [0]*(n-active)))
        return input_hash, expected_hash
    key = (n, p['index_pattern'])
    if key not in cache:
        primary = array.array('I', (finalizer((SEED+i+0x9E3779B97F4A7C15) & MASK64) & MASK32 for i in range(n)))
        if p['index_pattern'] == 'structured-v1':
            permutation = array.array('I', ((8191*i+17) % n for i in range(n)))
        else:
            permutation = array.array('I', range(n))
            state = SEED
            for i in range(n-1, 0, -1):
                state = (state + 0x9E3779B97F4A7C15) & MASK64
                j = finalizer(state) % (i+1)
                permutation[i], permutation[j] = permutation[j], permutation[i]
        input_hash = composite('primary_words', words_bytes(primary), 'permutation_words', words_bytes(permutation))
        expected = array.array('I', (primary[permutation[i]] ^ ((0x9E3779B9+i) & MASK32) for i in range(n)))
        gather_hash = digest(words_bytes(expected))
        for i in range(n):
            expected[permutation[i]] = primary[i] ^ ((0x9E3779B9+i) & MASK32)
        scatter_hash = digest(words_bytes(expected))
        cache[key] = input_hash, gather_hash, scatter_hash
    value = cache[key]
    return value[0], value[1 if p['variant'] == 'B1' else 2]

def audit():
    manifest_path = LOCAL / (BASE + '-manifest.json')
    ledger_path = LOCAL / (BASE + '-control.json')
    m = read_json(manifest_path)
    manifest_keys = 'manifest_version manifest_type manifest_id manifest_sha256 protocol_version machine_id child_executable_path supervisor_record_path expected_source_revision expected_git_dirty expected_child_executable_sha256 expected_supervisor_executable_sha256 expected_gpu_uuid cuda_device_ordinal vulkan_physical_device_index operation_timeout_ms child_timeout_ms campaign_timeout_ms continuation_policy declared_child_count children'.split()
    keys(m, manifest_keys, 'manifest')
    expected = dict(manifest_version=1, manifest_type='i7-correctness', manifest_id=BASE, protocol_version='1.1',
                    machine_id='i7-stage4-machine', child_executable_path='out/build/x64-debug/src/app/ComputeLabEx2Correctness.exe',
                    supervisor_record_path='results/local/' + BASE + '-control.json', expected_source_revision=SOURCE,
                    expected_git_dirty=False, expected_child_executable_sha256='cdb3b2b504bae0016117fc7f37cbeb777e42d1fc3cba917627679c8a5f513f29',
                    expected_supervisor_executable_sha256='1ad25840df454d0618d9ca6198755cf1939d187c73e989543fbcf855b931a66b',
                    expected_gpu_uuid=UUID, cuda_device_ordinal=0, vulkan_physical_device_index=0,
                    operation_timeout_ms=60000, child_timeout_ms=1200000, campaign_timeout_ms=86400000,
                    continuation_policy='completed-validation-only', declared_child_count=22)
    for key, value in expected.items():
        equal(m[key], value, 'manifest.' + key)
    payload = {k:v for k,v in m.items() if k != 'manifest_sha256'}
    equal(m['manifest_sha256'], digest(canonical(payload)), 'manifest canonical SHA')
    equal(m['manifest_sha256'], '5bbecef2058d4998de917839030f4402d1800a01dfc6bcc15332a0f0275def6d', 'frozen canonical SHA')
    children = [dict(sequence_index=i, core_cell_index=i, session_id=f'i7-stage4-c{i:02}',
                     expected_vulkan_shader_sha256=None if SHADER_MAP[i] is None else SHADERS[SHADER_MAP[i]]) for i in range(22)]
    equal(m['children'], children, 'schedule')
    require(file_hash(ROOT / m['child_executable_path']) == m['expected_child_executable_sha256'], 'child binary drift')
    require(file_hash(ROOT / 'out/build/x64-debug/src/app/ComputeLabEx2CorrectnessSupervisor.exe') == m['expected_supervisor_executable_sha256'], 'supervisor binary drift')
    for name, sha in zip(['A1','A2','B1','B2','C','D1'], SHADERS):
        equal(file_hash(ROOT / f'out/build/x64-debug/src/vulkan/Ex2{name}.comp.spv'), sha, 'shader.'+name)
    ledger = read_json(ledger_path)
    ledger_keys = 'record_version record_kind scope manifest_id manifest_sha256 manifest_type protocol_version machine_id continuation_policy expected_source_revision observed_source_revision expected_git_dirty observed_git_dirty expected_child_executable_sha256 actual_child_executable_sha256 expected_supervisor_executable_sha256 actual_supervisor_executable_sha256 expected_gpu_uuid verified_cuda_gpu_uuid verified_vulkan_gpu_uuid operation_timeout_ms child_timeout_ms campaign_timeout_ms start_time_utc end_time_utc scheduled_children executions overall_execution_status failure_reason'.split()
    keys(ledger, ledger_keys, 'ledger')
    ledger_expected = {k:m[k] for k in ledger_keys if k in m}
    ledger_expected.update(record_version=1, record_kind='ex2-i7-correctness-supervisor-execution',
        scope='correctness_control_only_no_stage4_verdict', observed_source_revision=SOURCE, observed_git_dirty=False,
        actual_child_executable_sha256=m['expected_child_executable_sha256'],
        actual_supervisor_executable_sha256=m['expected_supervisor_executable_sha256'],
        verified_cuda_gpu_uuid=UUID, verified_vulkan_gpu_uuid=UUID, overall_execution_status='stopped_incomplete_or_unsafe',
        failure_reason='stop_incomplete_or_unsafe', scheduled_children=children)
    for key, value in ledger_expected.items():
        equal(ledger[key], value, 'ledger.'+key)
    start = datetime.fromisoformat(ledger['start_time_utc'])
    end = datetime.fromisoformat(ledger['end_time_utc'])
    require(start <= end, 'ledger UTC order')
    require(len(ledger['executions']) == 17, 'actual launch count for preserved stopped campaign')
    for suffix in ['.incomplete','.incomplete.tmp']:
        require(not pathlib.Path(str(ledger_path)+suffix).exists(), 'ledger retained incomplete')
    artifact_rows = []
    series_rows = []
    bc_rows = []
    seen_artifacts = set()
    seen_runs = set()
    seen_series = set()
    seen_conditions = set()
    bc_cache = {}
    b_golden = composite('primary_words', words_bytes([0x12345678,0,0xffffffff,0xabcdef01]),
                         'permutation_words', words_bytes([2,0,3,1]))
    c_golden = composite('targets_words', words_bytes([3,0,3,1]), 'initial_counters_words', words_bytes([0]*4))
    equal(b_golden, 'c1b25198fd9f902e15f42b3d0080e893d4101f113855602720a63bf063a9e2de', 'B golden')
    equal(c_golden, '57cdcea826ef422677831a21be364ee2c44d203e533ac40c53b16f6d697fa9b7', 'C golden')
    previous_exit = start
    for i, record in enumerate(ledger['executions']):
        record_keys = list(children[i]) + 'launch_time_utc exit_time_utc exit_code termination_reason progress_validation cuda_attempt_started cuda_attempt_returned vulkan_attempt_started vulkan_attempt_returned package_state package_structurally_valid integrity_errors artifacts continuation_decision'.split()
        keys(record, record_keys, 'execution')
        failed = i == 16
        expected_record = dict(children[i], exit_code=3 if failed else 0, termination_reason=None, progress_validation='complete',
            cuda_attempt_started=True, cuda_attempt_returned=True, vulkan_attempt_started=True, vulkan_attempt_returned=True,
            package_state='incomplete' if failed else 'complete-pass', package_structurally_valid=True, integrity_errors=[],
            continuation_decision='stop_incomplete_or_unsafe' if failed else 'continue_complete_pass')
        for key, value in expected_record.items():
            equal(record[key], value, 'execution.'+key)
        launch = datetime.fromisoformat(record['launch_time_utc'])
        exit_time = datetime.fromisoformat(record['exit_time_utc'])
        require(previous_exit <= launch <= exit_time <= end, 'sequential child UTC order')
        previous_exit = exit_time
        session = record['session_id']
        session_dir = LOCAL / (session + '.incomplete' if failed else session)
        require(session_dir.is_dir() and not session_dir.is_junction(), 'retained session directory')
        if failed:
            require(not (LOCAL/session).exists(), 'failed session cannot be finalized')
            sidecar = read_json(LOCAL/(session+'.failure.json'))
            expected_sidecar = dict(record_version=1, record_type='ex2-i7-correctness-child-failure', session_id=session,
                failure_phase='validation', error_code='completed_validation_failed', foundation_established=True,
                source_revision=SOURCE, cuda_run_id=session+'-cuda', vulkan_run_id=session+'-vulkan', staging_retained=True,
                detail='native observation contradicts the preconstructed I7 identity')
            equal(sidecar, expected_sidecar, 'retained failure sidecar')
        else:
            for suffix in ['.incomplete','.failure.json']:
                require(not (LOCAL / (session+suffix)).exists(), 'child retained failure sibling')
        equal(sorted(p.name for p in session_dir.iterdir()), [session+'-cuda', session+'-vulkan'], 'session members')
        require(len(record['artifacts']) == (1 if failed else 8), 'retained artifact count per child')
        expected_paths = {f'results/local/{session}.failure.json'} if failed else {
            f'results/local/{session}/{session}-{backend}/{name}' for backend in ['cuda','vulkan']
            for name in ['environment.json','initialization.csv','samples.csv','summary.json']}
        equal({a['relative_path'] for a in record['artifacts']}, expected_paths, 'artifact membership')
        for a in record['artifacts']:
            keys(a, ['relative_path','size_bytes','sha256'], 'artifact')
            path = confined_regular(a['relative_path'])
            require(a['relative_path'] not in seen_artifacts, 'duplicate artifact')
            seen_artifacts.add(a['relative_path'])
            actual_size = path.stat().st_size
            actual_sha = file_hash(path)
            equal(actual_size, a['size_bytes'], 'artifact size')
            equal(actual_sha, a['sha256'], 'artifact SHA')
            backend = '' if failed else path.parent.name.rsplit('-',1)[1]
            artifact_rows.append(dict(sequence_index=i, core_cell_index=i, session_id=session, backend=backend,
                relative_path=a['relative_path'], ledger_size_bytes=a['size_bytes'], actual_size_bytes=actual_size,
                ledger_sha256=a['sha256'], actual_sha256=actual_sha, match=True))
        if failed:
            for backend in ['cuda','vulkan']:
                require(not list((session_dir/(session+'-'+backend)).iterdir()), 'no fabricated standard rows')
            print('AUDITED c16: exact external sidecar / two empty staging directories', flush=True)
            continue
        bc_expected = bc_reference(i, bc_cache) if 5 <= i <= 13 else None
        pair = []
        for slot, backend in enumerate(['cuda','vulkan']):
            directory = session_dir / (session+'-'+backend)
            equal(sorted(p.name for p in directory.iterdir()), ['environment.json','initialization.csv','samples.csv','summary.json'], 'series membership')
            env = read_json(directory / 'environment.json')
            keys(env, ENV_KEYS, 'environment')
            semantic = parameters(i)
            env_expected = dict(semantic, schema_version=2, experiment_id='EX-2', run_id=session+'-'+backend,
                git_commit=SOURCE, git_dirty=False, machine_id=m['machine_id'], protocol_version='1.1', evidence_kind='correctness',
                backend=backend, instrument_mode='P', warmup_count=0, planned_sample_count=1, block_index=0, process_index=0,
                order_slot=slot, source_revision=SOURCE, executable_sha256=m['expected_child_executable_sha256'],
                shader_sha256=None if backend == 'cuda' else children[i]['expected_vulkan_shader_sha256'], gpu_uuid_identity=UUID,
                validation_enabled=True, diagnostic_instrumentation=True, configure_preset='x64-debug', build_type='Debug')
            for key, value in env_expected.items():
                equal(env[key], value, 'environment.'+key)
            for key in ['input_sha256','expected_output_sha256','series_id','comparison_condition_id']:
                require(type(env[key]) is str and re.fullmatch('[0-9a-f]{64}', env[key]), 'digest format')
            for key in COMMON_KEYS:
                value = env[key]
                if key in ['schema_version','system_memory_bytes']:
                    require(type(value) is int and value >= 0, 'common integer')
                elif key in ['git_dirty','validation_enabled','diagnostic_instrumentation']:
                    require(type(value) is bool, 'common boolean')
                elif key == 'gpu_memory_bytes':
                    require(value is None or (type(value) is int and value > 0), 'GPU memory')
                else:
                    require(value is None or (type(value) is str and value), 'common string')
            require(env['gpu_name'] == 'NVIDIA GeForce RTX 2060 SUPER', 'target GPU name')
            require(re.fullmatch(r'\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z', env['timestamp_utc']), 'environment UTC')
            native = env['backend_native']
            keys(native, NATIVE_KEYS, 'backend_native')
            equal(native['native_markers_enabled'], False, 'native markers disabled')
            for key in ['timestamp_valid_bits','timestamp_period_ns','native_timing_method','native_timing_resolution_ns',
                        'native_timing_start_stage','native_timing_stop_stage','native_duration_envelope_ns']:
                equal(native[key], None, 'P timing.'+key)
            condition = dict(semantic, protocol_version='1.1', machine_id=m['machine_id'], gpu_uuid_identity=UUID, instrument_mode='P')
            equal(env['comparison_condition_id'], digest(canonical(condition)), 'condition identity')
            series = dict(condition, backend=backend, block_index=0, executable_sha256=m['expected_child_executable_sha256'],
                          order_slot=slot, planned_sample_count=1, process_index=0, shader_sha256=env['shader_sha256'],
                          source_revision=SOURCE, warmup_count=0)
            equal(env['series_id'], digest(canonical(series)), 'series identity')
            if bc_expected:
                equal(env['input_sha256'], bc_expected[0], 'independent B/C composite')
                equal(env['expected_output_sha256'], bc_expected[1], 'independent B/C expected')
            init = csv_rows(directory / 'initialization.csv', INIT_HEADER)
            init_expected = dict(schema_version='2', run_id=env['run_id'], experiment_id='EX-2', backend=backend,
                process_index='0', sequence_index='0', category='backend_setup', workload=semantic['workload'],
                variant=semantic['variant'], element_count=csv_value(semantic['element_count']), metric='setup_complete',
                duration_ns='', observation='setup_complete')
            equal(init, init_expected, 'initialization')
            sample = csv_rows(directory / 'samples.csv', SAMPLE_HEADER)
            sample_expected = {k:csv_value(env[k]) for k in SAMPLE_HEADER if k in env}
            sample_expected.update(sample_index='0', validation_passed='true', status='ok', failure_phase='', error_code='')
            sample_expected.update({k:'' for k in TIMINGS})
            equal(sample, sample_expected, 'raw sample')
            summary = read_json(directory / 'summary.json')
            group_keys = 'comparison_condition_id series_id run_id backend workload variant seed element_count byte_count index_pattern counter_count iteration_count transfer_direction execution_mode instrument_mode warmup_count planned_sample_count block_index order_slot process_index'.split()
            group = {k:env[k] for k in group_keys}
            admission = dict(gate0_admission='not_applicable', evidence_scope='correctness_only',
                             summary_sample_inclusion='inapplicable_no_timing_metrics')
            regenerated_summary = dict(schema_version=2, run_id=env['run_id'], experiment_id='EX-2', evidence_kind='correctness',
                process_status=sample['status'], failure_phase=None, error_code=None, sample_groups=[dict(group=group,
                    recorded_sample_count=1, validation_failures=int(sample['validation_passed']=='false'),
                    failed_sample_count=int(sample['status']!='ok'), metric_admission={k:admission for k in TIMINGS},
                    metrics={k:None for k in TIMINGS})])
            equal(summary, regenerated_summary, 'summary regenerated from raw row')
            require(env['run_id'] not in seen_runs and env['series_id'] not in seen_series, 'duplicate run/series')
            seen_runs.add(env['run_id'])
            seen_series.add(env['series_id'])
            seen_conditions.add(env['comparison_condition_id'])
            pair.append(env)
            series_rows.append(dict(sequence_index=i, core_cell_index=i, session_id=session, workload=semantic['workload'],
                variant=semantic['variant'], backend=backend, run_id=env['run_id'], comparison_condition_id=env['comparison_condition_id'],
                series_id=env['series_id'], protocol_version=env['protocol_version'], git_commit=env['git_commit'], git_dirty=env['git_dirty'],
                gpu_uuid=UUID, input_sha256=env['input_sha256'], expected_output_sha256=env['expected_output_sha256'],
                shader_sha256=env['shader_sha256'], validation_passed=True, status='ok', timing_fields_null=True,
                summary_regenerated=True, artifact_hashes_match=True, supervisor_package_state=record['package_state'], audit_result='PASS'))
        for key in ['comparison_condition_id','input_sha256','expected_output_sha256','git_commit','source_revision','gpu_uuid_identity','executable_sha256']:
            equal(pair[0][key], pair[1][key], 'pair.'+key)
        if bc_expected:
            bc_rows.append(dict(core_cell_index=i, session_id=session, variant=VARIANTS[i], input_sha256=bc_expected[0],
                                expected_output_sha256=bc_expected[1], independent_reconstruction='PASS'))
        print(f'AUDITED c{i:02}: 2 series / 8 artifact matches', flush=True)
    require(len(artifact_rows)==129 and len(series_rows)==32 and len(seen_conditions)==16, 'retained counts')
    require({p.name for p in LOCAL.glob('i7-stage4-c*')} == {c['session_id'] for c in children[:16]}
            | {'i7-stage4-c16.incomplete','i7-stage4-c16.failure.json'}, 'exact stopped campaign child topology')
    raw_hashes_after = {r['relative_path']:file_hash(ROOT/r['relative_path']) for r in artifact_rows}
    require(all(raw_hashes_after[r['relative_path']]==r['actual_sha256'] for r in artifact_rows), 'raw files changed during audit')
    ledger_sha = file_hash(ledger_path)
    results = dict(audit_result='PASS_FOR_RETAINED_ARTIFACTS_ONLY', stage4_outcome='INCOMPLETE',
        canonical_manifest_sha256=m['manifest_sha256'], manifest_file_sha256=file_hash(manifest_path),
        ledger_sha256=ledger_sha, scheduled_children=22, actual_children=17, completed_pairs=16, backend_series=32,
        standard_artifacts=128, artifact_hash_matches=129, summary_regenerations=32, passing_raw_rows=32,
        initialization_rows=32, distinct_run_ids=len(seen_runs), distinct_series_ids=len(seen_series), distinct_condition_ids=16,
        timing_values_present=0, incomplete_sessions=1, failure_sidecars=1, integrity_errors=0,
        b_golden_sha256=b_golden, c_golden_sha256=c_golden, independently_reconstructed_bc_cells=bc_rows,
        cpu_semantic_authority='Committed Prompt-3 independent inspector regenerated input/expected-output digests for the 16 complete-pass cells. The c16 E child failed before standard evidence serialization; c17..c21 did not launch.',
        observed_output_limitation='Schema v2 retains no observed GPU output bytes/digest/length. No independent disk reconstruction of wrong GPU output is claimed.')
    return results, artifact_rows, series_rows

def output_csv(name, rows):
    with (OUT/name).open('x', encoding='utf-8', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        for row in rows:
            writer.writerow({k:csv_value(v) for k,v in row.items()})

if __name__ == '__main__':
    try:
        result, artifacts, series = audit()
        output_csv('artifact-hashes.csv', artifacts)
        output_csv('series-audit.csv', series)
        with (OUT/'manifest-check.json').open('x', encoding='utf-8') as stream:
            json.dump(result, stream, indent=2)
            stream.write('\n')
        print(json.dumps(result, indent=2))
    except Exception as error:
        print('AUDIT FAILED: ' + str(error), file=sys.stderr)
        raise
