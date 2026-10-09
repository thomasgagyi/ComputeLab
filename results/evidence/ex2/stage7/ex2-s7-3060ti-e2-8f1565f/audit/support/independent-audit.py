"""Independent retained-evidence audit. No ComputeLab code imported or executed.
CPU only; NumPy acceleration checked against separately implemented scalar math.
Outputs are exclusive new files; original paths are read-only. Execute with -B.
"""
import csv, datetime, decimal, traceback, hashlib, io, json, math, os, pathlib, re, stat, struct, subprocess, sys
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[3]
CAM = 'ex2-s7-3060ti-e2-8f1565f'
WORK = pathlib.Path(__file__).parent
FINAL = ROOT/'results/local'/(CAM+'-postrun-audit-20261009')
REV = '8f1565f7fb92e2184f4d2382aabf8fa7d00e1274'
SEM = '99404552f6302a7273f991d02d6532ebd06b38a6e7224c17721278d6b00899aa'
PHY = 'd4d93217721285bbc7d55d8fc6853a3685e0d23f1f5e1b6203244c359122e1e1'
UUID = '99a2369e-ca50-aac6-5c2c-fcaa44625084'
SEED = 0x0123456789ABCDEF
U32, U64 = (1<<32)-1, (1<<64)-1
NAMES = ['environment.json','initialization.csv','samples.csv','summary.json']
METRICS = ['host_submission_ns','host_wait_ns','host_completion_ns']
SH = 'schema_version,run_id,experiment_id,comparison_condition_id,series_id,backend,workload,variant,seed,element_count,byte_count,index_pattern,counter_count,iteration_count,transfer_direction,execution_mode,instrument_mode,warmup_count,planned_sample_count,block_index,order_slot,process_index,sample_index,validation_passed,status,failure_phase,error_code,host_submission_ns,host_wait_ns,host_completion_ns,native_device_interval_ns'
IH = 'schema_version,run_id,experiment_id,backend,process_index,sequence_index,category,workload,variant,element_count,metric,duration_ns,observation'
CHECKS, DIFFS, PACKAGES, CONTROLS, LOGICAL, ANALYSIS = [], [], [], [], [], []

def sha(b): return hashlib.sha256(b).hexdigest()
def safe(p):
    p=pathlib.Path(p)
    assert p.resolve().is_relative_to(ROOT), 'outside worktree: '+str(p)
    for q in [p,*p.parents]:
        if q==ROOT.parent: break
        if q.exists(): assert not (q.lstat().st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT), 'reparse: '+str(q)
    return p
def read(p): return safe(p).read_bytes()
def duplicate(pairs):
    d={}
    for k,v in pairs:
        if k in d: raise ValueError('duplicate JSON key '+k)
        d[k]=v
    return d
def parse(b):
    return json.loads(b.decode('utf-8'),object_pairs_hook=duplicate,parse_constant=lambda x: (_ for _ in ()).throw(ValueError(x)))
def load(p): return parse(read(p))
def canonical(v, sorted_keys=False):
    # Independent shortest-roundtrip binary64 rendering. Frozen persisted values
    # in this campaign are finite and in fixed notation range; exponent spelling
    # is retained by repr when used. Exact bytes are compared, never tolerances.
    if v is None:return 'null'
    if type(v) is bool:return 'true' if v else 'false'
    if type(v) is int:return str(v)
    if type(v) is float:
        assert math.isfinite(v)
        d=decimal.Decimal(repr(v));fixed=format(d,'f')
        if '.' in fixed:fixed=fixed.rstrip('0').rstrip('.')
        if not d: return '0'
        mant,exp=format(d.normalize(),'e').split('e');sign='+' if int(exp)>=0 else '-'
        scientific=mant+'e'+sign+str(abs(int(exp))).zfill(2)
        return fixed if len(fixed)<=len(scientific) else scientific
    if isinstance(v,str):return json.dumps(v,ensure_ascii=False,separators=(',',':'))
    if isinstance(v,list):return '['+','.join(canonical(x,sorted_keys) for x in v)+']'
    assert isinstance(v,dict)
    keys=sorted(v) if sorted_keys else list(v)
    return '{'+','.join(canonical(k)+':'+canonical(v[k],sorted_keys) for k in keys)+'}'
def cb(v,sorted_keys=False):return canonical(v,sorted_keys).encode()
def req(b):assert type(b) is bool and b
def equal(a,b): assert type(a) is type(b) and a==b, f'expected {b!r}; observed {a!r}'
def check(name,func,path=None):
    try:
        detail=func()
        CHECKS.append({'check':name,'status':'PASS','path':str(path) if path else None,'detail':detail})
        return True
    except (FileNotFoundError,PermissionError) as e:
        status='UNVERIFIED'; detail=str(e)
    except Exception as e:
        status='FAIL';detail=type(e).__name__+': '+str(e)[:1000]+' ['+traceback.extract_tb(e.__traceback__)[-1].line+']'
    CHECKS.append({'check':name,'status':status,'path':str(path) if path else None,'detail':detail})
    DIFFS.append(CHECKS[-1]);return False
def write(p,v,raw=False):
    safe(p)
    b=v if raw else (json.dumps(v,ensure_ascii=False,sort_keys=True,indent=2,allow_nan=False)+'\n').encode()
    temp=p.with_name(p.name+'.publishing')
    assert not p.exists() and not temp.exists(), 'output collision '+str(p)
    with temp.open('xb') as f:f.write(b);f.flush();os.fsync(f.fileno())
    # Windows rename is atomic and refuses an existing destination.
    os.rename(temp,p)
    equal(read(p),b)
    return {'path':str(p),'size_bytes':len(b),'sha256':sha(b)}
def canonical_file(p,sorted_keys=False):
    b=read(p); req(not b.startswith(b'\xef\xbb\xbf') and b.endswith(b'\n') and not b.endswith(b'\n\n') and b'\r' not in b)
    d=parse(b);equal(b,cb(d,sorted_keys)+b'\n');return d
def snapshot(rows):
    out=[]
    for a in rows:
        p=safe(a['path']);b=read(p);s=p.stat()
        out.append({'path':str(p),'relative_path':p.relative_to(ROOT).as_posix(),'size_bytes':len(b),'sha256':sha(b),'mtime_ns':s.st_mtime_ns})
    return out
def gitstate():
    def g(*args):return subprocess.check_output(['git',*args],cwd=ROOT,text=True).strip()
    return {'head':g('rev-parse','HEAD'),'status':g('status','--porcelain=v1'),'detached':subprocess.run(['git','symbolic-ref','-q','HEAD'],cwd=ROOT,capture_output=True).returncode==1}

# Separately transcribed source-owned core cell table.
TABLE=[('A1',256,None),('A1',262144,None),('A1',16777216,None),('A2',262144,None),('A2',16777216,None),
       ('B1',262144,'structured-v1'),('B1',262144,'shuffled-v1'),('B1',16777216,'shuffled-v1'),
       ('B2',262144,'structured-v1'),('B2',262144,'shuffled-v1'),('B2',16777216,'shuffled-v1'),
       ('C',1048576,1048576),('C',1048576,32768),('C',1048576,64),('D1',262144,16),('D1',1048576,64),
       ('E1',1024,None),('E1',1048576,None),('E1',67108864,None),('E2',1024,None),('E2',1048576,None),('E2',67108864,None)]
def workload(c):
    v,n,x=TABLE[c]
    return dict(workload=v[0],variant=v,seed=SEED,element_count=None if v[0]=='E' else n,
                byte_count=n if v[0]=='E' else None,index_pattern=x if v[0]=='B' else None,
                counter_count=x if v=='C' else None,iteration_count=x if v[0]=='D' else None,
                transfer_direction=('H2D' if v=='E1' else 'D2H') if v[0]=='E' else None,
                execution_mode='prepared' if v[0]=='E' else 'ordinary',instrument_mode='H')
def boundary(c):
    return {'C':'atomic-dispatch-completion','D':'ordinary-iteration-sequence-completion','E':'prepared-single-copy-completion'}.get(TABLE[c][0][0],'single-dispatch-completion')
def plan(s):
    c=s//10;b=(s%10)//2;o=s%2;backend=['cuda','vulkan'][((b+o)%2)]
    return c,b,o,backend
def scalar_mix(seed,i):
    v=(seed+i+0x9E3779B97F4A7C15)&U64
    v=((v^(v>>30))*0xBF58476D1CE4E5B9)&U64
    v=((v^(v>>27))*0x94D049BB133111EB)&U64
    return v^(v>>31)
def scalar_a(x,i,heavy=False):
    y=x^((0x9E3779B9+i)&U32)
    if heavy:
        for _ in range(16):
            y=((y^(y>>16))*0x7FEB352D)&U32;y=((y^(y>>15))*0x846CA68B)&U32
        y^=y>>16
    return y
def scalar_d(x,i,k):
    for p in range(k):
        y=x^((0x9E3779B9+i+p)&U32);x=(((y<<5)|(y>>27))+0x7F4A7C15)&U32
    return x
def vector_mix(n,offset=0):
    a=np.arange(offset,offset+n,dtype=np.uint64)+np.uint64(SEED)+np.uint64(0x9E3779B97F4A7C15)
    a=(a^(a>>30))*np.uint64(0xBF58476D1CE4E5B9);a=(a^(a>>27))*np.uint64(0x94D049BB133111EB)
    return a^(a>>31)
def vector_a(x,idx,heavy=False):
    y=x^(np.uint32(0x9E3779B9)+idx)
    if heavy:
        for _ in range(16):
            y=(y^(y>>16))*np.uint32(0x7FEB352D);y=(y^(y>>15))*np.uint32(0x846CA68B)
        y^=y>>16
    return y
def vector_d(x,idx,k):
    y=x.copy()
    for p in range(k):
        t=y^(np.uint32(0x9E3779B9)+idx+np.uint32(p));y=((t<<5)|(t>>27))+np.uint32(0x7F4A7C15)
    return y
def frame(components):
    h=hashlib.sha256(b'ComputeLab/EX-2/logical-input/v1\0'+struct.pack('<I',2))
    for name,array in components:
        b=array.astype('<u4',copy=False).tobytes();s=name.encode()
        h.update(struct.pack('<I',len(s))+s+struct.pack('<Q',len(b)));h.update(b)
    return h.hexdigest()
def oracle_kat():
    expected=[0x157A3807A48FAA9D,0x9804297CC374CA1A,0xAF8D95523BECCAA2,0xCB4E5F6A912DCEF8]
    equal([scalar_mix(SEED,i) for i in range(4)],expected);equal(vector_mix(4).tolist(),expected)
    inp=np.array([x&U32 for x in expected],dtype=np.uint32);idx=np.arange(4,dtype=np.uint32)
    for heavy,lit in [(False,[0x3AB8D324,0x5D43B3A0,0xA5DBB319,0x0F1AB744]),(True,[0x4579A7C6,0x6D06CDCF,0x3BE3E55E,0x271DAF20])]:
        equal([scalar_a(int(x),i,heavy) for i,x in enumerate(inp)],lit);equal(vector_a(inp,idx,heavy).tolist(),lit)
    for k,lit in [(1,[0xD664E09C,0x27C0F020,0x3AC0DF49,0x62A16496]),(2,[0x89BDA0DE,0xBE3BAF8C,0x1E3F5AC9,0x120E2194])]:
        equal([scalar_d(int(x),i,k) for i,x in enumerate(inp)],lit);equal(vector_d(inp,idx,k).tolist(),lit)
    equal(scalar_d(int(inp[0]),0,16),0x8B2A8389);equal(scalar_d(int(inp[0]),0,64),0xB53390FE)
    equal(frame([('primary_words',np.array([0x12345678,0,U32,0xABCDEF01],dtype=np.uint32)),('permutation_words',np.array([2,0,3,1],dtype=np.uint32))]),'c1b25198fd9f902e15f42b3d0080e893d4101f113855602720a63bf063a9e2de')
    equal(frame([('targets_words',np.array([3,0,3,1],dtype=np.uint32)),('initial_counters_words',np.zeros(4,dtype=np.uint32))]),'57cdcea826ef422677831a21be364ee2c44d203e533ac40c53b16f6d697fa9b7')
    probes=[0,1,255,262143,16777215]
    for i in probes:
        equal(int(vector_mix(1,i)[0]),scalar_mix(SEED,i))
        for heavy in [False,True]:equal(int(vector_a(np.array([scalar_mix(SEED,i)&U32],dtype=np.uint32),np.array([i],dtype=np.uint32),heavy)[0]),scalar_a(scalar_mix(SEED,i)&U32,i,heavy))
    equal(permutation(4,'structured-v1').tolist(),[1,0,3,2]);equal(permutation(4,'shuffled-v1').tolist(),[3,0,2,1])
    perm=permutation(4,'structured-v1');equal(vector_a(inp[perm],idx).tolist(),[0x5D43B3A3,0x3AB8D327,0x0F1AB743,0xA5DBB31E])
    scatter=np.empty(4,dtype=np.uint32);scatter[perm]=vector_a(inp,idx);equal(scatter.tolist(),[0x5D43B3A0,0x3AB8D324,0x0F1AB744,0xA5DBB319])
    targets=(((np.arange(8,dtype=np.uint64)*8191+17)%8)%2).astype(np.uint32);equal(targets.tolist(),[1,0,1,0,1,0,1,0]);equal(np.bincount(targets,minlength=8).tolist(),[4,4,0,0,0,0,0,0])
    equal(vector_mix(4).astype(np.uint8).tolist(),[0x9D,0x1A,0xA2,0xF8])
    return 'literal mix64/A1/A2/B1/B2/C/D1/E and framing; independently written scalar versus vector boundary probes'
PERM_CACHE={}
def permutation(n,pattern):
    key=(n,pattern)
    if key in PERM_CACHE:return PERM_CACHE[key]
    if pattern=='structured-v1':a=((np.arange(n,dtype=np.uint64)*8191+17)%n).astype(np.uint32)
    else:
        a=np.arange(n,dtype=np.uint32);state=SEED
        # Full independent Fisher-Yates, scalar integer math; no producer helpers.
        for i in range(n,1,-1):
            state=(state+0x9E3779B97F4A7C15)&U64
            z=((state^(state>>30))*0xBF58476D1CE4E5B9)&U64
            z=((z^(z>>27))*0x94D049BB133111EB)&U64;j=(z^(z>>31))%i
            a[i-1],a[j]=a[j],a[i-1]
    equal(np.sort(a).tolist()==np.arange(n,dtype=np.uint32).tolist(),True)
    PERM_CACHE[key]=a;return a
def reconstruct(c):
    v,n,x=TABLE[c];w=workload(c);extra={}
    if v[0]=='E':
        h=hashlib.sha256()
        for offset in range(0,n,1048576):h.update(vector_mix(min(1048576,n-offset),offset).astype(np.uint8).tobytes())
        ih=oh=h.hexdigest();extra={'copy_bytes':n,'source_destination_identical':True}
    elif v=='C':
        target=(((np.arange(n,dtype=np.uint64)*8191+17)%n)%x).astype(np.uint32)
        zero=np.zeros(n,dtype=np.uint32);ih=frame([('targets_words',target),('initial_counters_words',zero)])
        out=np.bincount(target,minlength=n).astype('<u4');req(int(out.astype(np.uint64).sum())==n and not np.any(out[x:]) and int(target.max())<x)
        oh=sha(out.tobytes());extra={'total_updates':n,'inactive_zero_count':n-x,'allocated_counters':n}
    else:
        inp=vector_mix(n).astype(np.uint32);idx=np.arange(n,dtype=np.uint32)
        ih=sha(inp.astype('<u4').tobytes())
        if v[0]=='B':
            a=permutation(n,x);ih=frame([('primary_words',inp),('permutation_words',a)])
            if v=='B1':out=vector_a(inp[a],idx)
            else:out=np.empty(n,dtype=np.uint32);out[a]=vector_a(inp,idx)
            extra={'permutation_bijection':True,'permutation_range':[0,n-1],'permutation_sha256':sha(a.astype('<u4').tobytes())}
        elif v=='D1':out=vector_d(inp,idx,x);extra={'dependent_passes':x,'final_buffer':'StateA' if x%2==0 else 'StateB'}
        else:out=vector_a(inp,idx,v=='A2')
        oh=sha(out.astype('<u4').tobytes())
    fact={'cell_index':c,'workload':w,'input_sha256':ih,'expected_output_sha256':oh,**extra}
    LOGICAL.append(fact);return fact
def stats(values):
    values=list(map(float,values));n=len(values);s=0.0
    for v in values:s+=v
    mean=s/n;ss=0.0
    for v in values:d=v-mean;ss+=d*d
    sd=math.sqrt(ss/(n-1)) if n>1 else None;order=sorted(values)
    med=order[n//2] if n%2 else (order[n//2-1]+order[n//2])/2.0
    return dict(sample_count=n,minimum=order[0],median=med,mean=mean,standard_deviation=sd,coefficient_of_variation=sd/mean if mean and sd is not None else None,p95=order[math.ceil(.95*n)-1])
def csvread(p,header):
    b=read(p);req(b.endswith(b'\r\n') and b.replace(b'\r\n',b'').find(b'\r')<0 and b.replace(b'\r\n',b'').find(b'\n')<0)
    text=b.decode('utf-8');req(not text.startswith('\ufeff'))
    rows=list(csv.reader(io.StringIO(text,newline=''),strict=True));equal(rows[0],header.split(','));req(all(len(a)==len(rows[0]) for a in rows))
    out=io.StringIO(newline='');csv.writer(out,lineterminator='\r\n').writerows(rows);equal(out.getvalue().encode(),b)
    return [dict(zip(rows[0],a)) for a in rows[1:]]
def uint(s):
    req(isinstance(s,str) and bool(re.fullmatch(r'0|[1-9][0-9]*',s)));n=int(s);req(n<=U64);return n
def anchored(a):
    p=ROOT/a['relative_path'];b=read(p);equal(len(b),a['size_bytes']);equal(sha(b),a['sha256'])
    return b
def slot_audit(s,m,l,logical):
    c,b,o,backend=plan(s);run=CAM+f'-slot-{s:03d}';p=ROOT/'results/local'/run;w=workload(c);sl=l['slots'][s]
    equal(set(sl),set('sequence_index session_id control_phase entered_revision process_revision inspection_revision terminal_revision disposition process inspection reconciliation_reason continuation'.split()))
    equal(set(sl['process']),set('process_created containment_assigned containment_verified process_resumed process_id exit_code launch_time_utc exit_time_utc exit_kind progress_form progress_transcript active_attempt last_returned_attempt progress_invalid progress_transport_failed clean_eof trailing_bytes operation_timed_out child_timed_out campaign_timed_out timed_out_sample_index supervisor_termination_requested terminate_job_succeeded primary_termination_confirmed job_total_processes job_active_processes descendant_survival_observed job_empty_confirmed containment_verification_failed control_error'.split()))
    equal(set(sl['inspection']),set('attempted topology structurally_valid package_finalized location exact_file_set scientific_bundle_parsed scientific_bundle_valid scientific_bytes_canonical changed_during_inspection artifacts package_sha256 science sidecar_present sidecar_valid sidecar_artifact sidecar errors'.split()))

    equal(sorted(x.name for x in safe(p).iterdir()),sorted(NAMES))
    for x in p.iterdir():safe(x);req(x.is_file())
    equal(sl['sequence_index'],s);equal(sl['session_id'],run)
    # Initial running revision 1, four transitions per child plus one per prior completed cell.
    entered=2+4*s+c
    for key,value in [('entered_revision',entered),('process_revision',entered+1),('inspection_revision',entered+2),('terminal_revision',entered+3)]:equal(sl[key],value)
    equal(sl['control_phase'],'resolved');equal(sl['disposition'],'resolved_success');equal(sl['continuation'],'schedule_complete' if s==219 else 'launch_next')
    inv=[]
    for n,a in zip(NAMES,sl['inspection']['artifacts']):
        equal(a['name'],n);equal(a['relative_path'],p.relative_to(ROOT).as_posix()+'/'+n);anchored(a)
        inv.append({k:a[k] for k in ['name','size_bytes','sha256']})
    equal(len(sl['inspection']['artifacts']),4)
    ph=sha(cb({'package_hash_version':1,'artifacts':inv},True));equal(ph,sl['inspection']['package_sha256'])
    e=canonical_file(p/'environment.json');summary=canonical_file(p/'summary.json');rows=csvread(p/'samples.csv',SH);init=csvread(p/'initialization.csv',IH)
    common='schema_version experiment_id run_id timestamp_utc git_commit git_dirty machine_id os_name os_version cpu_name system_memory_bytes gpu_name gpu_vendor gpu_device_id gpu_memory_bytes nvidia_driver_version cuda_toolkit_version cuda_runtime_version cuda_compute_capability vulkan_sdk_version vulkan_device_api_version compiler_name compiler_version cmake_version ninja_version configure_preset build_type validation_enabled diagnostic_instrumentation'.split()
    ext='protocol_version evidence_kind backend instrument_mode warmup_count planned_sample_count comparison_condition_id series_id cell_index slot_sequence_index block_index process_index order_slot source_revision executable_sha256 shader_sha256 input_sha256 expected_output_sha256 gpu_uuid_identity workload variant generator_revision seed element_count byte_count index_pattern counter_count iteration_count transfer_direction execution_mode operation_boundary backend_native'.split()
    equal(list(e),common+ext)
    equal(list(e['backend_native']), 'implementation stream_flags queue_family_index queue_flags queue_count timestamp_valid_bits timestamp_period_ns input_memory_flags output_memory_flags upload_memory_flags readback_memory_flags native_markers_enabled native_timing_method native_timing_resolution_ns native_timing_start_stage native_timing_stop_stage native_duration_envelope_ns'.split())

    shader=m['expected_vulkan_shader_sha256'].get(w['variant'].lower()) if backend=='vulkan' else None
    fields=dict(schema_version=2,experiment_id='EX-2',run_id=run,git_commit=REV,git_dirty=False,machine_id=m['machine_id'],
                protocol_version='1.4',evidence_kind='diagnostic',backend=backend,instrument_mode='H',warmup_count=0,planned_sample_count=100,
                cell_index=c,slot_sequence_index=s,block_index=b,process_index=b,order_slot=o,source_revision=REV,
                executable_sha256=m['expected_child_executable_sha256'],shader_sha256=shader,input_sha256=logical['input_sha256'],
                expected_output_sha256=logical['expected_output_sha256'],gpu_uuid_identity=UUID,generator_revision='ex2-mix64-v1',operation_boundary=boundary(c),**{k:v for k,v in w.items() if k!='instrument_mode'})
    for k,v in fields.items():equal(e[k],v)
    equal(e['os_name'],'Windows');equal(e['os_version'],'10.0.19045');equal(e['cpu_name'].strip(),'AMD Ryzen 7 5700X 8-Core Processor');
    equal(e['gpu_name'],'NVIDIA GeForce RTX 3060 Ti');equal(e['gpu_vendor'],'NVIDIA');equal(e['nvidia_driver_version'],'616.92')
    equal(e['configure_preset'],'x64-release');equal(e['build_type'],'Release');equal(e['compiler_name'],'MSVC');equal(e['compiler_version'],'19.51.36260.0')
    for k in ['validation_enabled','diagnostic_instrumentation']:equal(e[k],False)
    req(type(e['system_memory_bytes']) is int and e['system_memory_bytes']>0 and type(e['gpu_memory_bytes']) is int and e['gpu_memory_bytes']>0)
    req(bool(re.fullmatch(r'\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z',e['timestamp_utc'])))
    if backend=='cuda':equal(e['cuda_toolkit_version'],'13.4.92');equal(e['cuda_compute_capability'],'8.6');equal(e['cuda_runtime_version'],'13.4')
    else:equal(e['vulkan_sdk_version'],'1.4.363');req(isinstance(e['vulkan_device_api_version'],str))
    for k in ['cmake_version','ninja_version','gpu_device_id']:
        req(isinstance(e[k],str) and bool(e[k]) and not any(x in e[k] for x in ['\\','/','\r','\n','\0']))
    for k in ['cuda_toolkit_version','cuda_runtime_version','cuda_compute_capability','vulkan_sdk_version','vulkan_device_api_version']:
        req(e[k] is None or isinstance(e[k],str))
    native=e['backend_native'];equal(native['implementation'],'ex2-'+backend+'-'+w['variant'].lower()+'-native');req(native['native_markers_enabled'] is False)
    for k in ['timestamp_valid_bits','timestamp_period_ns','native_timing_method','native_timing_resolution_ns','native_timing_start_stage','native_timing_stop_stage','native_duration_envelope_ns']:equal(native[k],None)
    for k in ['queue_family_index','queue_flags','queue_count','input_memory_flags','output_memory_flags','upload_memory_flags','readback_memory_flags']:
        req(native[k] is None or (type(native[k]) is int and 0<=native[k]<=U32))
    if backend=='cuda':equal(native['stream_flags'],'nonblocking');req(all(native[k] is None for k in ['queue_family_index','queue_flags','queue_count']))
    else:req(native['stream_flags'] is None and type(native['queue_family_index']) is int and type(native['queue_flags']) is int and type(native['queue_count']) is int and native['queue_count']>0)
    cond={k:e[k] for k in ['byte_count','counter_count','element_count','execution_mode','generator_revision','gpu_uuid_identity','index_pattern','instrument_mode','iteration_count','machine_id','operation_boundary','protocol_version','seed','transfer_direction','variant','workload']}
    cid=sha(cb(cond,True));equal(e['comparison_condition_id'],cid)
    series={**cond,**{k:e[k] for k in ['backend','block_index','executable_sha256','order_slot','planned_sample_count','process_index','shader_sha256','source_revision','warmup_count']}}
    sid=sha(cb(series,True));equal(e['series_id'],sid)
    equal(len(rows),100);vals={k:[] for k in METRICS}
    for i,row in enumerate(rows):
        for k in SH.split(','):
            if k in ['sample_index','validation_passed','status','failure_phase','error_code',*METRICS,'native_device_interval_ns']:continue
            value=e[k] if k in e else 'EX-2';expect='' if value is None else str(value)
            equal(row[k],expect)
        equal(uint(row['sample_index']),i);equal(row['validation_passed'],'true');equal(row['status'],'ok')
        for k in ['failure_phase','error_code','native_device_interval_ns']:equal(row[k],'')
        for k in METRICS:vals[k].append(uint(row[k]))
        req(vals[METRICS[0]][-1]+vals[METRICS[1]][-1]<=U64);equal(vals[METRICS[2]][-1],vals[METRICS[0]][-1]+vals[METRICS[1]][-1])
    req(len(init)>0);req(sum(row['metric']=='setup_complete' for row in init)<=1)
    for row in init:
        if row['metric']=='setup_complete':equal(row['category'],'backend_setup');equal(row['duration_ns'],'');req(bool(row['observation']))
    for i,row in enumerate(init):
        for k,v in dict(schema_version='2',run_id=run,experiment_id='EX-2',backend=backend,process_index=str(b),sequence_index=str(i)).items():equal(row[k],v)
        req(bool(row['duration_ns']) or bool(row['observation']))
        if row['duration_ns']:uint(row['duration_ns'])
        req(bool(row['category'] and row['metric']))
        if row['workload']:equal(row['workload'],w['workload'])
        if row['variant']:equal(row['variant'],w['variant'])
        if row['element_count']:equal(row['element_count'],str(w['element_count']))
    group={k:e[k] for k in ['comparison_condition_id','series_id','run_id','cell_index','slot_sequence_index','backend','workload','variant','seed','element_count','byte_count','index_pattern','counter_count','iteration_count','transfer_direction','execution_mode','instrument_mode','warmup_count','planned_sample_count','block_index','order_slot','process_index']}
    ms={k:stats(vals[k]) for k in METRICS}
    regen=dict(schema_version=2,run_id=run,experiment_id='EX-2',protocol_version='1.4',evidence_kind='diagnostic',process_status='ok',failure_phase=None,error_code=None,
               sample_groups=[dict(group=group,recorded_sample_count=100,successful_sample_count=100,validation_failures=0,failed_sample_count=0,metrics={**ms,'native_device_interval_ns':None})])
    equal(read(p/'summary.json'),cb(regen)+b'\n');req(summary==regen)
    windows=[dict(begin=i,count=25,successful_row_count=25,host_completion_median_ns=stats(vals['host_completion_ns'][i:i+25])['median']) for i in [0,25,50,75]]
    pr=sl['process'];ins=sl['inspection'];req(type(pr['process_id']) is int and 0<pr['process_id']<=U32)
    for k in ['process_created','process_resumed','primary_termination_confirmed','containment_assigned','containment_verified','job_empty_confirmed','clean_eof']:equal(pr[k],True)
    for k in ['containment_verification_failed','progress_invalid','progress_transport_failed','operation_timed_out','child_timed_out','campaign_timed_out']:equal(pr[k],False)
    for k in ['active_attempt','control_error','timed_out_sample_index']:equal(pr[k],None)
    equal(pr['job_active_processes'],0);req(pr['job_total_processes'] is None or (type(pr['job_total_processes']) is int and 1<=pr['job_total_processes']<=U32))
    equal(pr['exit_code'],0);equal(pr['trailing_bytes'],0);equal(pr['progress_form'],'Full');equal(pr['last_returned_attempt'],dict(sample_index=99,slot_sequence_index=s))
    cleanup=pr['descendant_survival_observed']
    if cleanup:
        equal(pr['supervisor_termination_requested'],True);equal(pr['terminate_job_succeeded'],True);req(pr['exit_kind'] in ['SupervisorForced','VoluntaryStage6']);reason='resolved_success_post_completion_cleanup'
    else:
        equal(pr['supervisor_termination_requested'],False);equal(pr['exit_kind'],'VoluntaryStage6');reason='resolved_success'
    equal(sl['reconciliation_reason'],reason)
    for k in ['attempted','structurally_valid','package_finalized','exact_file_set','scientific_bundle_parsed','scientific_bundle_valid','scientific_bytes_canonical']:equal(ins[k],True)
    for k in ['changed_during_inspection','sidecar_present','sidecar_valid']:equal(ins[k],False)
    equal(ins['location'],'Final');equal(ins['topology'],'FinalOnly');equal(ins['errors'],[]);equal(ins['sidecar'],None);equal(ins['sidecar_artifact'],None)
    equal(ins['science'],dict(error_code=None,failure_phase=None,process_status='ok',recorded_sample_count=100,terminal_state='Success'))
    events=pr['progress_transcript'];equal(len(events),200);last=lastreceive=0
    for j,a in enumerate(events):
        equal(set(a),set(['child_qpc','event','magic','receive_qpc','reserved','sample_index','slot_sequence_index','version']))
        for k,v in dict(magic=0x53365043,version=1,reserved=0,event=1+j%2,sample_index=j//2,slot_sequence_index=s).items():equal(a[k],v)
        req(type(a['child_qpc']) is int and type(a['receive_qpc']) is int and 0<a['child_qpc']<=a['receive_qpc']<=(1<<63)-1 and a['child_qpc']>=last and a['receive_qpc']>=lastreceive)
        wire=struct.pack('<IIIIQQq',a['magic'],a['version'],a['event'],a['reserved'],s,j//2,a['child_qpc']);equal(len(wire),40)
        last=a['child_qpc'];lastreceive=a['receive_qpc']
        req(l['campaign_start_counter']<=a['child_qpc']<=a['receive_qpc']<l['campaign_deadline_counter'])
        if j%2:req(a['receive_qpc']-events[j-1]['child_qpc'] <= l['qpc_frequency']*60)
    start=datetime.datetime.fromisoformat(pr['launch_time_utc'].replace('Z','+00:00'));end=datetime.datetime.fromisoformat(pr['exit_time_utc'].replace('Z','+00:00'))
    req(0<=(end-start).total_seconds()<=1200);req(start>=datetime.datetime.fromisoformat(l['campaign_start_time_utc'].replace('Z','+00:00')) and end<=datetime.datetime.fromisoformat(l['campaign_end_time_utc'].replace('Z','+00:00')))
    if s:
        req(pr['launch_time_utc']>=l['slots'][s-1]['process']['exit_time_utc']);req(events[0]['child_qpc']>=l['slots'][s-1]['process']['progress_transcript'][-1]['receive_qpc'])
    fact=dict(slot=s,backend=backend,cell=c,block=b,process=b,order=o,package_path=str(p),package_sha256=ph,series_id=sid,condition_id=cid,rows=100,correctness_valid_rows=100,summary_status='ok',cleanup_reason=reason if cleanup else None,summary_regenerated_sha256=sha(cb(regen)+b'\n'),metrics=ms,windows=windows)
    PACKAGES.append(fact);CONTROLS.append(dict(slot=s,process_id=pr['process_id'],reason=reason,cleanup=cleanup,events=200,launch_time_utc=pr['launch_time_utc'],exit_time_utc=pr['exit_time_utc'],job_total_processes=pr['job_total_processes'],revision_anchors=[entered,entered+1,entered+2,entered+3]))
    return dict(slot_sequence_index=s,block_index=b,order_slot=o,process_index=b,package_sha256=ph,series_id=sid,terminal_state='success',recorded_sample_count=100,successful_sample_count=100,validation_failures=0,failed_sample_count=0,failure_phase=None,error_code=None,metrics=ms,windows=windows)

def main():
    before=load(WORK/'original-evidence-before.json');extra=load(WORK/'preparation-authority-before.json')
    write(WORK/(pathlib.Path(__file__).stem+('-review-spec.json' if '--review' in sys.argv else '-spec.json')),dict(campaign=CAM,source_revision=REV,semantic_manifest_sha256=SEM,physical_manifest_sha256=PHY,table=TABLE,authority='pinned source and protocol; no producer code executed',no_gpu=True))
    check('clean detached pinned source before',lambda:equal(gitstate(),before['source']))
    check('sealed input identity at audit start',lambda:equal(snapshot(before['files']),before['files']))
    check('supplemental preparation inventory at audit start',lambda:equal(snapshot(extra['files']),extra['files']))
    mp=ROOT/'results/local'/(CAM+'-stage6-manifest.json');lp=ROOT/'results/local'/(CAM+'-stage6-control.json')
    m=load(mp);l=load(lp)
    def manifest():
        equal(canonical_file(mp,True),m);equal(sha(read(mp)),PHY);equal(sha(cb({k:v for k,v in m.items() if k!='manifest_sha256'},True)),SEM)
        expected=dict(manifest_version=1,manifest_type='ex2-stage6-diagnostic',manifest_id=CAM,manifest_sha256=SEM,protocol_version='1.4',evidence_schema_version=2,evidence_kind='diagnostic',instrument_mode='H',machine_id='ex2-s7-3060ti-machine',child_executable_path='out/build/x64-release/src/app/ComputeLabEx2Stage6.exe',expected_source_revision=REV,expected_git_dirty=False,expected_child_executable_sha256='4104e5f7237b03d5ac3265f450358e76533b099e4fdc049e62cd9ff20e9ae93c',expected_supervisor_executable_sha256='33a0bebcba66e02e2f39ee4b311844c748e1a9b0c828a9c49b5e57ea989de5c8',expected_gpu_uuid=UUID,cuda_device_ordinal=0,vulkan_physical_device_index=0,expected_vulkan_shader_sha256=m['expected_vulkan_shader_sha256'],operation_timeout_ms=60000,child_timeout_ms=1200000,campaign_timeout_ms=86400000,continuation_policy='resolved-only-no-retry',declared_cell_group_count=22,declared_child_count=220,groups=[dict(cell_index=c,declared_child_count=10,children=[dict(sequence_index=s,session_id=CAM+f'-slot-{s:03d}') for s in range(c*10,c*10+10)]) for c in range(22)])
        equal(m,expected);equal(set(m),set(expected))
        equal(read(mp),read(ROOT/'results/tmp'/(CAM+'-prep')/'candidate-manifest.json'))
    check('strict canonical manifest and separately reconstructed full schedule',manifest,mp)
    authority=load(ROOT/'results/local/ex2-s7-e2-single-execution-20261009/frozen-authority.json')
    def artifacts():
        equal(authority['source'],REV);equal(authority['manifest_id'],CAM)
        equal(len(authority['artifacts']),8)
        for key,a in authority['artifacts'].items():
            data=read(ROOT/a['path']);equal(len(data),a['size_bytes']);equal(sha(data),a['sha256'])
            equal(a['sha256'],m['expected_'+key+'_executable_sha256'] if key in ['child','supervisor'] else m['expected_vulkan_shader_sha256'][key])
        freeze=load(ROOT/'results/local'/(CAM+'-preparation-20261009')/'e2-artifact-freeze.json');equal(freeze['artifacts'],authority['artifacts']);equal(freeze['source_revision'],REV)
    check('all eight frozen Release artifacts against manifest and receipts',artifacts)
    def ledger():
        equal(set(l),set('record_version record_type protocol_version manifest_id manifest_sha256 manifest_file_sha256 manifest resume_policy ledger_revision control_state control_start_time_utc campaign_start_time_utc campaign_end_time_utc qpc_frequency campaign_start_counter campaign_deadline_counter observed_preflight fatal_sequence_index fatal_reason slots cell_analysis campaign_analysis'.split()))
        equal(canonical_file(lp,True),l);equal(l['manifest'],m);equal(l['manifest_sha256'],SEM);equal(l['manifest_file_sha256'],PHY);equal(l['manifest_id'],CAM)
        for k,v in dict(record_version=1,record_type='ex2-stage6-supervisor-control',protocol_version='1.4',resume_policy='forbidden',control_state='completed',ledger_revision=905,fatal_reason=None,fatal_sequence_index=None).items():equal(l[k],v)
        equal(len(l['slots']),220);equal(len(l['cell_analysis']),22);equal(1+220*4+22+1+1,l['ledger_revision'])
        f=l['observed_preflight'];equal(f,dict(child_executable_sha256=m['expected_child_executable_sha256'],supervisor_executable_sha256=m['expected_supervisor_executable_sha256'],cuda_uuid=UUID,vulkan_uuid=UUID,git_dirty=False,source_revision=REV,vulkan_shader_sha256=m['expected_vulkan_shader_sha256']))
        req(type(l['qpc_frequency']) is int and l['qpc_frequency']>0);equal(l['campaign_deadline_counter']-l['campaign_start_counter'],l['qpc_frequency']*86400)
    check('terminal ledger canonical identity, revisions and preflight',ledger,lp)
    def topology():
        observed={x.name for x in (ROOT/'results/local').iterdir() if x.name.startswith(CAM+'-slot-')}
        equal(observed,{CAM+f'-slot-{s:03d}' for s in range(220)})
        for suffix in ['-stage6-control.json.incomplete','-stage6-control.json.incomplete.tmp']:req(not (ROOT/'results/local'/(CAM+suffix)).exists())
        ap=ROOT/'results/local'/(CAM+'-stage6-analysis');equal({x.name for x in ap.iterdir()},{f'cell-{c:02d}-analysis.json' for c in range(22)}|{'campaign-analysis.json'})
    check('E2 topology, staging absence and exact analysis set',topology)
    check('independent literal known answers and scalar/vector equivalence',oracle_kat)
    check('shortest decimal serializer known answers',lambda: equal([canonical(float(x)) for x in [100000,500000,5011,0.125]],['1e+05','5e+05','5011','0.125']))
    reconstructed={};descriptions={}
    for c in range(22):
        print('regenerating logical cell',c,flush=True)
        def logical(c=c):reconstructed[c]=reconstruct(c)
        check(f'cell {c:02d} complete independent input/output reconstruction',logical)
    for s in range(220):
        def package(s=s):descriptions[s]=slot_audit(s,m,l,reconstructed[s//10])
        check(f'slot {s:03d} package/control/progress/raw/summary/windows',package,ROOT/'results/local'/(CAM+f'-slot-{s:03d}'))
    def cell(c):
        backends={}
        for backend in ['cuda','vulkan']:
            pp=sorted([descriptions[s] for s in range(c*10,c*10+10) if plan(s)[3]==backend],key=lambda p:p['process_index'])
            meds=[p['metrics']['host_completion_ns']['median'] for p in pp];req(len(pp)==5 and min(meds)>0)
            backends[backend]=dict(resolved_process_count=5,successful_process_count=5,diagnostic_failure_process_count=0,process_median_span_ratio=max(meds)/min(meds),processes=pp)
        data=dict(analysis_version=1,protocol_version='1.4',analysis_kind='stage6-cell-diagnostic',cell_index=c,workload=workload(c),status='complete',resolved_slot_count=10,successful_slot_count=10,diagnostic_failure_slot_count=0,backends=backends)
        a=l['cell_analysis'][c];equal(a['relative_path'],f'results/local/{CAM}-stage6-analysis/cell-{c:02d}-analysis.json');equal(anchored(a),cb(data)+b'\n')
        ANALYSIS.append(dict(cell_index=c,path=a['relative_path'],regenerated_sha256=sha(cb(data)+b'\n'),size_bytes=len(cb(data)+b'\n')))
    for c in range(22):check(f'cell {c:02d} exact diagnostic analysis reconstruction',lambda c=c:cell(c))
    def campaign():
        req(len(descriptions)==220 and len(ANALYSIS)==22)
        data=dict(analysis_version=1,protocol_version='1.4',analysis_kind='stage6-campaign-diagnostic',status='complete',declared_slots=220,attempted_slots=220,resolved_slots=220,successful_slots=220,diagnostic_failure_slots=0,unlaunched_slots=0,fully_resolved_cells=22,cells_with_diagnostic_failures=0,cell_analysis_sha256=[a['regenerated_sha256'] for a in ANALYSIS],claim_scope=dict(cross_backend_performance_admitted=False,stage2_authorized=False,production_backend_selected=False))
        a=l['campaign_analysis'];equal(a['relative_path'],f'results/local/{CAM}-stage6-analysis/campaign-analysis.json');equal(anchored(a),cb(data)+b'\n');return a
    check('exact canonical campaign analysis and claim firewall',campaign)
    prep_chain(m,l,authority)
    check('retained producer inventories independently anchored to actual bytes',lambda: receipt_anchors(before,extra))
    check('summary mathematical known answer',lambda: equal(stats([1,1,3,3]),dict(sample_count=4,minimum=1.0,median=2.0,mean=2.0,standard_deviation=math.sqrt(4/3),coefficient_of_variation=math.sqrt(4/3)/2,p95=3.0)))
    # Detect new or removed production files in addition to byte/mtime drift.
    def e2_paths():
        out=set()
        for base in ['results/local','results/tmp']:
            for p in (ROOT/base).iterdir():
                if (CAM in p.name or p.name=='ex2-s7-e2-single-execution-20261009') and p not in [WORK,FINAL]:
                    safe(p)
                    if p.is_file():out.add(str(p))
                    else:
                        for d,ds,fs in os.walk(p,followlinks=False):
                            for n in ds+fs:safe(pathlib.Path(d)/n)
                            out.update(str(pathlib.Path(d)/n) for n in fs)
        return out
    expectedpaths={a['path'] for a in before['files'] if a['relative_path'].startswith(('results/local/'+CAM,'results/tmp/'+CAM,'results/local/ex2-s7-e2-single-execution-20261009'))}
    check('sealed E2 path set unchanged',lambda:equal(e2_paths(),expectedpaths))
    after={'source':gitstate(),'files':snapshot(before['files'])};afterextra={'files':snapshot(extra['files'])}
    check('all original bytes, sizes, paths and mtimes unchanged',lambda:equal(after,before))
    check('all shared historical preparation authority unchanged',lambda:equal(afterextra['files'],extra['files']))
    check('audit outputs ignored and confined to fresh roots',lambda: req(all(subprocess.run(['git','check-ignore',str(p)],cwd=ROOT,capture_output=True).returncode==0 for p in [WORK,FINAL])))
    if '--review' in sys.argv:
        write(WORK/'review-004.json',dict(checks=CHECKS,discrepancies=DIFFS,packages=PACKAGES,logical=LOGICAL,analyses=ANALYSIS))
        print(json.dumps(dict(check_counts={x:sum(c['status']==x for c in CHECKS) for x in ['PASS','FAIL','UNVERIFIED']},discrepancies=DIFFS),indent=2),flush=True)
        return
    write(FINAL/'original-evidence-before.json',before);write(FINAL/'original-evidence-after.json',after)
    write(FINAL/'preparation-authority-before.json',extra);write(FINAL/'preparation-authority-after.json',afterextra)
    write(FINAL/'package-audit-facts.json',PACKAGES);write(FINAL/'process-control-audit-facts.json',CONTROLS)
    write(FINAL/'logical-input-regeneration-facts.json',LOGICAL);write(FINAL/'summary-and-analysis-reconstruction-facts.json',ANALYSIS)
    write(FINAL/'discrepancies.json',DIFFS)
    verdict='REJECTED' if any(x['status']=='FAIL' for x in CHECKS) else 'NOT ACCEPTED / INCOMPLETE' if any(x['status']=='UNVERIFIED' for x in CHECKS) else 'ACCEPTED'
    counts=dict(packages_verified=len(PACKAGES),raw_rows_verified=sum(x['rows'] for x in PACKAGES),correctness_valid_rows=sum(x['correctness_valid_rows'] for x in PACKAGES),progress_events_verified=sum(x['events'] for x in CONTROLS),logical_cells_regenerated=len(LOGICAL),process_summaries_regenerated=len(PACKAGES),windows_regenerated=sum(len(x['windows']) for x in PACKAGES),cell_analyses_regenerated=len(ANALYSIS),campaign_analyses_regenerated=int(any(x['check']=='exact canonical campaign analysis and claim firewall' and x['status']=='PASS' for x in CHECKS)),cleanup_slots=[x['slot'] for x in CONTROLS if x['cleanup']])
    matrix={x:sum(c['status']==x for c in CHECKS) for x in ['PASS','FAIL','UNVERIFIED']}
    limitations=['Retained validation flags plus independently reconstructed expected digests are correctness evidence, not a fresh GPU correctness rerun; actual output buffers are not retained.',
                 'Progress validation independently interprets retained ledger event records; original pipe byte streams are not separately retained. QPC does not replace scientific host timing values.',
                 'No external continuous OS process monitor is claimed. One launch/no retry is established only within retained supervisor, task and wrapper evidence.',
                 'Operator NVIDIA recording-off attestation and OBS-off probe are contextual provenance, not proof of universal absence of overhead.',
                 'Avast restoration is a retained reminder unless an actual restoration attestation is present; environmental restoration is not a scientific correctness gate.',
                 'Shared Phase-A records explicitly retain incomplete canonical Debug coverage and missing full Debug build console retention; prior bounded Release smoke receipts exist, and this audit reruns no tests.',
                 'E1 remains permanently INCOMPLETE; historical shared artifact receipts are not E2 scientific evidence and historical E1 manifest identities remain distinct.',
                 'Gate 0 FAIL, Stage 2 NOT GRANTED, production backend UNSELECTED; no ranking, speedup, cross-generation or production-runtime inference.']
    index=None
    if verdict=='ACCEPTED':index=write(FINAL/'ex2-s7-3060ti-e2-accepted-process-index.json',{'campaign':CAM,'record_count':220,'processes':PACKAGES})
    result=dict(audit_identity=CAM+'-independent-postrun-20261009',verdict=verdict,campaign=CAM,source_revision=REV,manifest_semantic_sha256=SEM,manifest_physical_sha256=PHY,counts=counts,check_counts=matrix,checks=CHECKS,discrepancies=DIFFS,limitations=limitations,accepted_index=index,prohibited_actions_performed=[],next_human_controlled_step='Separate curation review only after acceptance; no benchmark inference')
    write(FINAL/'ex2-s7-3060ti-e2-independent-postrun-audit.json',result)
    report(result,authority,before,extra)
    write(FINAL/'independent-audit.py',read(pathlib.Path(__file__)),True)
    write(FINAL/'audit-spec.json',load(WORK/(pathlib.Path(__file__).stem+'-spec.json')))
    output=[]
    for base in [WORK,FINAL]:
        for p in sorted(base.iterdir()):
            if p.is_file():b=read(p);output.append(dict(path=str(p),size_bytes=len(b),sha256=sha(b)))
    receipt=dict(audit_verdict=verdict,code_sha256=sha(read(pathlib.Path(__file__))),input_inventory_sha256=sha(read(FINAL/'original-evidence-before.json')),checks=CHECKS,check_counts=matrix,outputs=output,self_digest_excluded_to_avoid_circular_hash=True,python=sys.version,numpy=np.__version__)
    a=write(FINAL/'audit-receipt.json',receipt)
    print(json.dumps({'verdict':verdict,'counts':counts,'check_counts':matrix,'discrepancies':DIFFS,'receipt':a},indent=2),flush=True)

def prep_chain(m,l,authority):
    d=ROOT/'results/local/ex2-s7-e2-single-execution-20261009';prep=ROOT/'results/local'/(CAM+'-preparation-20261009');ex=ROOT/'results/tmp'/(CAM+'-execution')
    def preparation():
        for p in [prep/'independent-prelaunch-verification.json',prep/'e2-preparation-handoff.json',d/'fast-final-preflight.json',ex/'point-of-use-preflight.json',ex/'immediate-precreation-preflight.json']:
            a=load(p)
            for k in ['semantic_sha256']:equal(a[k],SEM)
            equal(a['physical_sha256'],PHY)
            if 'manifest_id' in a:equal(a['manifest_id'],CAM)
            if 'artifacts' in a:equal(a['artifacts'],authority['artifacts'])
        auth=load(d/'operator-launch-confirmation.json');equal(auth['authorization'],f'AUTHORIZE S7-3060TI-E2 {CAM} {SEM} {PHY}');equal(auth['semantic'],SEM);equal(auth['physical'],PHY);equal(auth['manifest_id'],CAM);equal(auth['nvidia_recording_features_off_checked'],True)
        verify=load(prep/'independent-prelaunch-verification.json');equal(verify['candidate_and_final_identical'],True);equal(verify['source_revision'],REV)
        expected_schedule=[]
        for s in range(220):
            c,b,o,backend=plan(s);expected_schedule.append(dict(backend=backend,block_index=b,cell_index=c,order_slot=o,planned_samples=100,process_index=b,sequence_index=s,session_id=CAM+f'-slot-{s:03d}',warmup=0))
        equal(verify['schedule'],expected_schedule);equal(verify['planned_observations'],22000);equal(verify['manifest_bytes'],len(read(ROOT/'results/local'/(CAM+'-stage6-manifest.json'))))
        for key,a in verify['artifact_inventory'].items():
            original=authority['artifacts'][key];equal(a['path'],str(ROOT/original['path']));equal(a['bytes'],original['size_bytes']);equal(a['sha256'],original['sha256'])

        return 'E2 candidate bytes and frozen identity independently checked; prelaunch PASS is not sole proof'
    check('E2 candidate/final/precreation/authorization anchors',preparation)
    def launch():
        intent=load(ex/'launch-intent.json');created=load(ex/'supervisor-created.json');terminal=load(ex/'supervisor-terminal.json');wr=load(ex/'wrapper-terminal.json');tr=load(d/'terminal-execution-receipt.json')
        argv=[str(ROOT/'out/build/x64-release/src/app/ComputeLabEx2Stage6Supervisor.exe'),'--manifest',f'results/local/{CAM}-stage6-manifest.json']
        equal(intent['argv'],argv);equal(created['argv'],argv);equal(created['cwd'],str(ROOT));equal(intent['cwd'],str(ROOT));equal(intent['manifest_id'],CAM)
        equal(intent['no_retry_no_resume'],True);equal(intent['single_creation_only'],True);equal(created['status'],'LAUNCHED_ONCE');equal(created['pid'],terminal['pid']);equal(terminal['exit_code'],0);equal(wr['supervisor_exit_code'],0)
        equal(created['start_utc'],'2026-10-09T13:07:23.381699+00:00');equal(terminal['end_utc'],'2026-10-09T13:21:58.012108+00:00');equal(tr['created'],created);equal(tr['native_terminal'],terminal)
        equal(tr['dispatch_count'],1);equal(tr['native_supervisor_creations'],1);equal(tr['retry_resume_performed'],False);equal(tr['ledger_revision'],905);equal(tr['ledger_state'],'completed')
        equal(tr['ledger_file_identity']['sha256'],sha(read(ROOT/'results/local'/(CAM+'-stage6-control.json'))));equal(tr['ledger_file_identity']['bytes'],len(read(ROOT/'results/local'/(CAM+'-stage6-control.json'))))
        reg=load(d/'task-registration-check.json');equal(reg['triggers'],0);equal(reg['restarts'],0)
        import xml.etree.ElementTree as ET
        x=ET.fromstring(reg['xml']);ns={'t':'http://schemas.microsoft.com/windows/2004/02/mit/task'}
        equal(len(x.find('t:Triggers',ns)),0);req(x.find('t:Settings/t:RestartOnFailure',ns) is None);equal(x.find('t:Settings/t:MultipleInstancesPolicy',ns).text,'IgnoreNew')
        equal(x.find('t:Actions/t:Exec/t:WorkingDirectory',ns).text,str(ROOT));req('single-use-execution.py' in x.find('t:Actions/t:Exec/t:Arguments',ns).text and 'execute-once' in x.find('t:Actions/t:Exec/t:Arguments',ns).text)
        ri=load(d/'task-registration-intent.json');equal(ri['script_sha256'],sha(read(d/'single-use-execution.py')))
        equal(load(d/'dispatch-intent.json')['one_start_request_only'],True);equal(load(d/'dispatch-returned.json')['request_returned'],True)
        script=read(d/'register-dispatch-e2-once.ps1').decode('utf-8-sig');equal(len(re.findall(r'(?im)^\s*Start-ScheduledTask\b',script)),1)
        equal(tr['counts']['resolved_success'],220);equal(tr['counts']['process_creations_recorded'],220)
        req(created['start_utc']<l['campaign_start_time_utc']);req(l['campaign_end_time_utc']<terminal['end_utc'])
        return 'one retained task dispatch and one native creation; sequential 220 child records, no retry within retained evidence'
    check('original one-launch scheduled task and native terminal reconciliation',launch)
    def probe():
        a=load(ex/'probe-receipt.json');equal(a['exit_code'],0);result=a['result'];equal(result['environment_controls']['DISABLE_VULKAN_OBS_CAPTURE'],'1')
        cu=result['cuda'][0];equal(cu['uuid'],UUID);equal(cu['ordinal'],0);equal(cu['compute_capability'],'8.6')
        # Find Vulkan GPU records without executing the probe.
        vk=result['vulkan'];req(any(v.get('uuid')==UUID and v.get('index')==0 for v in vk));equal(result['module_capture_while_instance_alive'],True);equal(result['obs_absent'],True);req(not any('graphics-hook' in x.lower() or 'obs' in pathlib.Path(x).name.lower() for x in result['loaded_relevant_modules']))
        equal(load(ex/'power-restoration.json')['restored'],True)
        return 'retained metadata-only same-GPU/OBS-off context and AC sleep restoration'
    check('retained execution metadata probe and restoration context',probe)
    def historical():
        h=ROOT/'results/local/ex2-s7-preparation-20261009-h2'
        a=load(h/'artifact-freeze.json');req(REV in json.dumps(a));req(all(z['sha256'] in json.dumps(a) for z in authority['artifacts'].values()))
        identities=[]
        for n in ['candidate-verification.json','final-verification.json','stability-verification.json']:
            a=load(h/n);equal(a['source_revision'],REV);equal(a['artifacts'],authority['artifacts']);equal(a['manifest_id'],'ex2-s7-3060ti-e1-8f1565f');identities.append((a['semantic_sha256'],a['physical_sha256']))
        req(len(set(identities))==1);req(identities[0]!=(SEM,PHY))

        contract=read(h/'deployment-contract.md').decode();req('22 frozen' in contract and '60000 ms' in contract);equal(a['source_revision'],REV);req('-arch=native' in read(h/'release-build.ninja').decode());req('sm_86.cubin' in read(h/'release-cuda-elf.log').decode());equal(load(h/'artifact-freeze.json')['provenance']['cuda_target'],'native sm_86')
        cache=read(h/'release-cache.txt').decode();req('VCPKG_TARGET_TRIPLET' in cache and 'Visual Studio' in cache)
        return 'shared artifact build authority verified; historical E1 manifest not conflated with E2'
    check('shared Phase-A candidate/final/stability/deployment artifact authority',historical)


def receipt_anchors(before,extra):
    sealed={a['path']:a for a in before['files']+extra['files']}
    records=[]
    roots=[ROOT/'results/local'/(CAM+'-preparation-20261009'),ROOT/'results/local/ex2-s7-e2-single-execution-20261009',ROOT/'results/local/ex2-s7-preparation-20261009-h2']
    for p in [roots[0]/'preparation-output-inventory.json',roots[1]/'terminal-evidence-inventory.json',roots[2]/'preparation-index.json']:
        data=load(p);items=data.get('files',data.get('retained_files'))
        if isinstance(items,dict):items=[dict(path=k,**v) for k,v in items.items()]
        for a in items:
            target=pathlib.Path(a['path']); key=str(target)
            if 'ex2-s7-3060ti-e1-' in key:
                records.append(dict(path=key,disposition='outside E2 scientific dataset: historical E1 anchor'));continue
            req(key in sealed)
            b=read(target);equal(sha(b),a['sha256']);equal(len(b),a.get('bytes',a.get('size_bytes')))
            records.append(dict(path=key,sha256=sha(b),size_bytes=len(b),disposition='verified'))
    write(WORK/(pathlib.Path(__file__).stem+('-review-anchor-facts.json' if '--review' in sys.argv else '-anchor-facts.json')),records)
    return dict(verified=sum(x['disposition']=='verified' for x in records),historical_e1_excluded=sum(x['disposition']!='verified' for x in records))

def report(r,authority,before,extra):
    sections=[
      ('Audit identity, independence and evidence authority','Independent newly written Python/NumPy audit, pinned source consulted as contract. No producer serializer, parser, oracle or verifier executed. Read-only source and retained data; CPU reconstruction only.'),
      ('Exact E2 frozen identities',f'Campaign {CAM}; source {REV}; machine ex2-s7-3060ti-machine; RTX 3060 Ti, UUID {UUID}; CUDA0/Vulkan0. Semantic manifest {SEM}; physical manifest {PHY}.\n\n'+json.dumps(authority['artifacts'],indent=2)),
      ('Preparation and original human authorization chain','See individually checked candidate/final/precreation, exact operator authorization and shared Phase-A artifact authority rows in the check matrix. Historical prelaunch not-executed state is appropriate for its timestamp.'),
      ('Expected schedule and observed topology','Source-owned 22 cells x 10 children; CV/VC/CV/VC/CV; 220 declared unique routes; exact four files per package. Audit counts below include only successfully completed independent checks.'),
      ('Ledger state/revision/slot consistency','Reported completed revision 905; independent transition arithmetic: running 1 + 880 slot transitions + 22 cell publications + campaign publication 1 + terminal publication 1. Exact per-slot anchors checked.'),
      ('Ordinary versus DR-45 cleanup dispositions','Discovered cleanup slots: '+str(r['counts']['cleanup_slots'])+'. Ordinary and cleanup cases use separate strict predicates; no process excluded.'),
      ('Progress and native control','Each checked success requires 200 Started/Returned frames, exact 40-byte source grammar, indexes, QPC constraints, clean EOF, no trailing bytes or outstanding attempt, final Job ActiveProcesses=0 and strict containment/exit/deadline predicates.'),
      ('Package structures, hashes and identities','Per-slot package facts include independently framed four-artifact package hashes, condition/series identities, provenance and regenerated summary hashes. Ledger artifact anchors independently checked.'),
      ('Raw H rows and correctness evidence','All independently checked rows retain original order and extremes. Validation true/status ok; unsigned timing values; exact completion=submission+wait; native interval empty/null. No filtering or benchmark interpretation.'),
      ('Logical input/output reconstruction','Full mix64 inputs, all B permutations and bijections, framed B/C logical identities, N-counter histograms with inactive zeros, all D passes and final buffer parity, and all copy bytes reconstructed. Literal known answers and separately written scalar calculations check vector arithmetic.'),
      ('Process summaries and positional windows','Sequential binary64 sums, two-pass sample SD, even median and nearest-rank p95 reconstructed; exact summary bytes checked. Four fixed 25-row windows per process independently reconstructed.'),
      ('Cell and campaign analysis reproductions','Source field order and exact numeric serialization independently reconstructed for each analysis; compare exact bytes, length, SHA-256 and ledger relative path anchors. Within-backend process median span is the only ratio regenerated.'),
      ('Claim firewall','Gate 0 FAIL; Stage 2 NOT GRANTED; production backend UNSELECTED. All canonical campaign claim flags false. No cross-API or cross-generation performance claims.'),
      ('E2/E1 separation and no-retry record','Only E2 packages in this acceptance dataset. E1 permanently INCOMPLETE, revision 20 authoritative; never adopt temporary revision 21. One-launch/no-retry claims are bounded by retained records.'),
      ('Before/after immutability','Primary sealed inventory: '+str(len(before['files']))+' files, plus '+str(len(extra['files']))+' shared historical preparation records. All bytes/sizes/mtimes and E2 path set checked after reads, including eight artifacts, manifest and clean detached source.'),
      ('Discrepancies, uncertainties and limitations','Preliminary review-001/review-002 records are validator development diagnostics, not evidence verdicts. Original evidence was never adjusted to fit the validator. All implementation versions remain retained.\n\n'+json.dumps(r['discrepancies'],indent=2)+'\n\n'+'\n\n'.join(r['limitations'])),
      ('Check matrix and receipt hashes','Counts: '+json.dumps(r['check_counts'])+'. Exact implementation/input/output hashes are in audit-receipt.json; its own digest is returned externally to avoid a circular self hash.'),
      ('FINAL VERDICT',r['verdict']),
      ('Accepted process index',json.dumps(r['accepted_index'],indent=2) if r['accepted_index'] else 'No accepted-process index issued.'),
      ('NEXT HUMAN-CONTROLLED STEP',r['next_human_controlled_step'])]
    text='# E2 independent full-campaign post-run audit\n\n'+json.dumps(r['counts'],indent=2)+'\n\n'
    for i,(title,body) in enumerate(sections,1):text+=f'## {i}. {title}\n\n{body}\n\n'
    text+='| Check | Status | Detail |\n|---|---|---|\n'
    for x in CHECKS:text+='| '+x['check']+' | '+x['status']+' | '+str(x['detail']).replace('|','/').replace('\n',' ')+' |\n'
    text+='\nNO GPU WORK / NO CAMPAIGN LAUNCH / NO HISTORICAL EDITS\n\nNO CURATION / NO BENCHMARK INTERPRETATION / NO COMMIT / NO PUSH\n'
    write(FINAL/'ex2-s7-3060ti-e2-independent-postrun-audit.md',text.encode(),True)

if __name__=='__main__':main()
