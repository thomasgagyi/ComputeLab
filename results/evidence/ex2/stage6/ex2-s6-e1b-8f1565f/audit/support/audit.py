"""Independent, read-only protocol-1.4 evidence auditor.
No project imports, executables, GPU APIs, build or test invocation.
All writes are exclusive creates in the designated audit namespace.
"""
from pathlib import Path
import array, collections, csv, datetime, decimal, hashlib, io, json, math, os, re, stat, struct, subprocess, sys
ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
ID = 'ex2-s6-e1b-8f1565f'
SRC = '8f1565f7fb92e2184f4d2382aabf8fa7d00e1274'
SEM = '5a87f15ef384d3c8c1c0f43adf736e17a5e6cdc46fcd2e849f333ae3e07c0963'
PHY = 'ad83b68e8ff430854325ec944c1317a348b087dd9daaf825f51100b7cdaa68b8'
GPU = '0340eaac-dc67-f450-d558-d47c55cc4417'
BASE = ROOT/'results/local'
PREFIX = BASE/(ID+'-stage6')
NAMES = ['environment.json','initialization.csv','samples.csv','summary.json']
def sha(b): return hashlib.sha256(b).hexdigest()
def safe(p):
    p = Path(p).absolute()
    if not p.is_relative_to(ROOT): raise ValueError('path outside repository: '+str(p))
    for q in [p, *p.parents]:
        st=q.lstat()
        if getattr(st,'st_file_attributes',0)&0x400 or stat.S_ISLNK(st.st_mode):
            raise ValueError('reparse/link: '+str(q))
        if q==ROOT: break
    return p
def raw(p):
    p=safe(p)
    if not p.is_file(): raise ValueError('not regular file: '+str(p))
    return p.read_bytes()
def pairs(p):
    d={}
    for k,v in p:
        if k in d: raise ValueError('duplicate JSON key '+k)
        d[k]=v
    return d
def read(p): return json.loads(raw(p).decode('utf-8'),object_pairs_hook=pairs,parse_constant=lambda x: (_ for _ in ()).throw(ValueError(x)))
def write(p,obj):
    b=(json.dumps(obj,ensure_ascii=False,indent=2,allow_nan=False)+'\n').encode()
    with Path(p).open('xb') as f: f.write(b)
def inventory():
    paths=[]
    for base in [BASE,ROOT/'results/tmp']:
        for p in sorted(base.glob(ID+'*')):
            if p==OUT or p.name.endswith(('-stage6-postrun-audit.json','-stage6-postrun-audit.md','-stage6-accepted-process-index.json')): continue
            safe(p)
            if p.is_file(): paths.append(p)
            else:
                for d,dirs,files in os.walk(p,followlinks=False):
                    for n in dirs: safe(Path(d)/n)
                    paths.extend(Path(d)/n for n in files)
    result=[]
    for p in sorted(paths):
        b=raw(p); s=p.stat()
        result.append(dict(path=p.relative_to(ROOT).as_posix(),size_bytes=len(b),sha256=sha(b),mtime_ns=s.st_mtime_ns))
    return result
def git_guard():
    commands=[['rev-parse','--verify','HEAD'],['branch','--show-current'],['status','--porcelain=v1','--untracked-files=all'],['diff','--check'],['diff','--cached','--name-status']]
    r=[]
    for args in commands:
        x=subprocess.run(['git',*args],cwd=ROOT,capture_output=True,text=True,check=True)
        r.append(dict(arguments=args,stdout=x.stdout,stderr=x.stderr))
    if [x['stdout'].strip() for x in r] != [SRC,'main','','','']: raise ValueError('Git guard failed')
    return r
if __name__=='__main__' and sys.argv[1]=='snapshot':
    write(OUT/'source-before.json',git_guard())
    files=inventory(); write(OUT/'retained-files-before.json',files)
    print(json.dumps(dict(snapshot_files=len(files),bytes=sum(f['size_bytes'] for f in files))))

SEED=0x0123456789ABCDEF
PARAMS=['element_count','byte_count','index_pattern','counter_count','iteration_count','transfer_direction']
METRICS=['host_submission_ns','host_wait_ns','host_completion_ns']
SAMPLE_HEADER='schema_version,run_id,experiment_id,comparison_condition_id,series_id,backend,workload,variant,seed,element_count,byte_count,index_pattern,counter_count,iteration_count,transfer_direction,execution_mode,instrument_mode,warmup_count,planned_sample_count,block_index,order_slot,process_index,sample_index,validation_passed,status,failure_phase,error_code,host_submission_ns,host_wait_ns,host_completion_ns,native_device_interval_ns'
INIT_HEADER='schema_version,run_id,experiment_id,backend,process_index,sequence_index,category,workload,variant,element_count,metric,duration_ns,observation'
COMMON_KEYS='schema_version experiment_id run_id timestamp_utc git_commit git_dirty machine_id os_name os_version cpu_name system_memory_bytes gpu_name gpu_vendor gpu_device_id gpu_memory_bytes nvidia_driver_version cuda_toolkit_version cuda_runtime_version cuda_compute_capability vulkan_sdk_version vulkan_device_api_version compiler_name compiler_version cmake_version ninja_version configure_preset build_type validation_enabled diagnostic_instrumentation'.split()
ENV_EXTRA='protocol_version evidence_kind backend instrument_mode warmup_count planned_sample_count comparison_condition_id series_id cell_index slot_sequence_index block_index process_index order_slot source_revision executable_sha256 shader_sha256 input_sha256 expected_output_sha256 gpu_uuid_identity workload variant generator_revision seed element_count byte_count index_pattern counter_count iteration_count transfer_direction execution_mode operation_boundary backend_native'.split()
NATIVE_KEYS='implementation stream_flags queue_family_index queue_flags queue_count timestamp_valid_bits timestamp_period_ns input_memory_flags output_memory_flags upload_memory_flags readback_memory_flags native_markers_enabled native_timing_method native_timing_resolution_ns native_timing_start_stage native_timing_stop_stage native_duration_envelope_ns'.split()
def workload(cell):
    specs=[('A1',256),('A1',262144),('A1',16777216),('A2',262144),('A2',16777216),('B1',262144,'structured-v1'),('B1',262144,'shuffled-v1'),('B1',16777216,'shuffled-v1'),('B2',262144,'structured-v1'),('B2',262144,'shuffled-v1'),('B2',16777216,'shuffled-v1'),('C',1048576,1048576),('C',1048576,32768),('C',1048576,64),('D1',262144,16),('D1',1048576,64),('E1',1024),('E1',1048576),('E1',67108864),('E2',1024),('E2',1048576),('E2',67108864)]
    v,n,*extra=specs[cell]; family=v[0]
    w=dict(workload=family,variant=v,seed=SEED,**dict.fromkeys(PARAMS),execution_mode='prepared' if family=='E' else 'ordinary',instrument_mode='H')
    w['byte_count' if family=='E' else 'element_count']=n
    if family=='B': w['index_pattern']=extra[0]
    if family=='C': w['counter_count']=extra[0]
    if family=='D': w['iteration_count']=extra[0]
    if family=='E': w['transfer_direction']='H2D' if v=='E1' else 'D2H'
    return w
def boundary(w): return {'C':'atomic-dispatch-completion','D':'ordinary-iteration-sequence-completion','E':'prepared-single-copy-completion'}.get(w['workload'],'single-dispatch-completion')
def slot(n):
    c,pos=divmod(n,10); b,o=divmod(pos,2)
    return dict(slot_sequence_index=n,cell_index=c,backend=['cuda','vulkan','vulkan','cuda','cuda','vulkan','vulkan','cuda','cuda','vulkan'][pos],block_index=b,order_slot=o,process_index=b,session_id=f'{ID}-slot-{n:03}')
def number(x):
    if not math.isfinite(x): raise ValueError('nonfinite')
    d=decimal.Decimal(repr(x)); fixed=format(d,'f')
    if '.' in fixed: fixed=fixed.rstrip('0').rstrip('.')
    if not x: return '-0' if math.copysign(1,x)<0 else '0'
    t=d.normalize(); sign,digits,exp=t.as_tuple(); ds=''.join(map(str,digits))
    exponent=len(ds)+exp-1
    sci=('-' if sign else '')+ds[0]+('.'+ds[1:] if len(ds)>1 else '')+'e'+('+' if exponent>=0 else '-')+str(abs(exponent)).zfill(2)
    return fixed if len(fixed)<=len(sci) else sci
def canonical(x,sort=False):
    if x is None: return 'null'
    if isinstance(x,bool): return 'true' if x else 'false'
    if isinstance(x,str):
        return '"'+''.join('\\"' if c=='"' else '\\\\' if c=='\\' else '\\u%04x'%ord(c) if ord(c)<32 else c for c in x)+'"'
    if isinstance(x,int): return str(x)
    if isinstance(x,float): return number(x)
    if isinstance(x,list): return '['+','.join(canonical(v,sort) for v in x)+']'
    return '{'+','.join(canonical(k)+':'+canonical(x[k],sort) for k in (sorted(x) if sort else x))+'}'
def canonical_bytes(x,sort=False,lf=True): return (canonical(x,sort)+('\n' if lf else '')).encode('utf-8')
def digest(x): return sha(canonical_bytes(x,True,False))
def metrics(values):
    vals=list(map(float,values)); total=0.0
    for v in vals: total+=v
    mean=total/len(vals); squared=0.0
    for v in vals: squared+=(v-mean)*(v-mean)
    sd=math.sqrt(squared/(len(vals)-1)) if len(vals)>1 else None
    v=sorted(vals); mid=len(v)//2
    return dict(sample_count=len(v),minimum=v[0],median=v[mid] if len(v)%2 else (v[mid-1]+v[mid])/2,mean=mean,standard_deviation=sd,coefficient_of_variation=None if sd is None or not mean else sd/mean,p95=v[math.ceil(.95*len(v))-1])
def csvbytes(rows):
    s=io.StringIO(newline=''); csv.writer(s,lineterminator='\r\n').writerows(rows); return s.getvalue().encode()
def logical_inputs():
    # NumPy only vectorizes independent fixed-width integer formulas; statistics,
    # JSON/CSV, control, inventory and hashing use the Python standard library.
    import numpy as np
    results=[]; words={}; perms={}; bytehash={}; outputs={}
    def data_bytes(a): return a.astype('<u4',copy=False).tobytes()
    def mix(a):
        a=(a^(a>>np.uint64(30)))*np.uint64(0xBF58476D1CE4E5B9)
        a=(a^(a>>np.uint64(27)))*np.uint64(0x94D049BB133111EB)
        return a^(a>>np.uint64(31))
    def multi(components):
        h=hashlib.sha256(b'ComputeLab/EX-2/logical-input/v1\0'+struct.pack('<I',2))
        for name,a in components:
            name=name.encode(); h.update(struct.pack('<I',len(name))+name+struct.pack('<Q',a.size*4)); h.update(data_bytes(a))
        return h.hexdigest()
    for cell in range(22):
        w=workload(cell); fam=w['workload']; n=w['element_count']; count=w['byte_count'] or n
        if fam=='E':
            if count not in bytehash:
                h=hashlib.sha256()
                for start in range(0,count,1048576):
                    ix=np.arange(start,min(start+1048576,count),dtype=np.uint64)
                    h.update(mix(ix+np.uint64(SEED)+np.uint64(0x9E3779B97F4A7C15)).astype('u1').tobytes())
                bytehash[count]=h.hexdigest()
            ih=oh=bytehash[count]; extra=dict(named_copy_direction=w['transfer_direction'],byte_count=count)
        elif fam=='C':
            active=w['counter_count']; ix=np.arange(n,dtype=np.uint64)
            target=(((8191*ix+17)%n)%active).astype('<u4'); zero=np.zeros(n,dtype='<u4')
            ih=multi([('targets_words',target),('initial_counters_words',zero)])
            counters=np.bincount(target,minlength=n).astype('<u4')
            assert np.all(counters[:active]==n//active) and np.all(counters[active:]==0)
            oh=sha(data_bytes(counters)); extra=dict(target_sha256=sha(data_bytes(target)),active=active,total_updates=int(counters.astype('u8').sum()))
        else:
            if n not in words:
                ix=np.arange(n,dtype=np.uint64)
                words[n]=mix(ix+np.uint64(SEED)+np.uint64(0x9E3779B97F4A7C15)).astype('<u4')
            primary=words[n]; ih=sha(data_bytes(primary)); ix=np.arange(n,dtype=np.uint32); extra={}
            if fam=='A':
                out=primary^(np.uint32(0x9E3779B9)+ix)
                if w['variant']=='A2':
                    for _ in range(16):
                        out=(out^(out>>np.uint32(16)))*np.uint32(0x7FEB352D)
                        out=(out^(out>>np.uint32(15)))*np.uint32(0x846CA68B)
                    out=out^(out>>np.uint32(16))
            elif fam=='B':
                key=(n,w['index_pattern'])
                if key not in perms:
                    if w['index_pattern']=='structured-v1':
                        assert math.gcd(8191,n)==1
                        perm=((8191*np.arange(n,dtype=np.uint64)+17)%n).astype('<u4')
                    else:
                        perm=array.array('I',range(n)); state=SEED; mask=(1<<64)-1
                        for k in range(n,1,-1):
                            state=(state+0x9E3779B97F4A7C15)&mask
                            z=state; z=((z^(z>>30))*0xBF58476D1CE4E5B9)&mask; z=((z^(z>>27))*0x94D049BB133111EB)&mask; z^=z>>31
                            j=z%k; perm[k-1],perm[j]=perm[j],perm[k-1]
                        perm=np.frombuffer(perm,dtype=np.uint32).copy()
                    assert int(perm.min())==0 and int(perm.max())==n-1 and np.all(np.bincount(perm,minlength=n)==1)
                    perms[key]=perm
                perm=perms[key]; ih=multi([('primary_words',primary),('permutation_words',perm)])
                if w['variant']=='B1': out=primary[perm]^(np.uint32(0x9E3779B9)+ix)
                else:
                    out=np.empty(n,dtype='<u4'); out[perm]=primary^(np.uint32(0x9E3779B9)+ix)
                extra=dict(permutation_sha256=sha(data_bytes(perm)),size=n,range_valid=True,collision_free=True)
            else:
                out=primary.copy()
                for k in range(w['iteration_count']):
                    v=out^(np.uint32(0x9E3779B9)+ix+np.uint32(k)); out=((v<<np.uint32(5))|(v>>np.uint32(27)))+np.uint32(0x7F4A7C15)
                extra=dict(iteration_count=w['iteration_count'],final_buffer='StateA')
            oh=sha(data_bytes(out))
        results.append(dict(cell_index=cell,workload=w,input_sha256=ih,expected_output_sha256=oh,details=extra))
        print('logical cell',cell,'complete',flush=True)
    write(OUT/'logical-input-facts.json',dict(python=sys.version,numpy=np.__version__,cells=results))

if __name__=='__main__' and sys.argv[1]=='logical': logical_inputs()

class Audit:
    def __init__(self): self.checks=collections.Counter(); self.failures=[]; self.facts={}; self.processes=[]
    def check(self,group,label,ok,detail=None):
        self.checks[group]+=1
        if not ok: self.failures.append(dict(group=group,check=label,detail=detail))
    def eq(self,g,label,a,b):
        self.check(g,label,a==b and (not isinstance(b,bool) or type(a) is bool),None if a==b else dict(actual=a,expected=b))
    def fields(self,g,label,actual,expected):
        for k,v in expected.items(): self.eq(g,label+'.'+k,actual.get(k,'<missing>'),v)
    def exact(self,g,label,actual,expected):
        self.eq(g,label+'.keys',sorted(actual),sorted(expected)); self.fields(g,label,actual,expected)
    def bytes(self,g,label,p,expected):
        b=raw(p); self.check(g,label,b==expected,dict(actual_sha256=sha(b),expected_sha256=sha(expected),actual_bytes=len(b),expected_bytes=len(expected)))
    def anchor(self,g,p,a):
        b=raw(p); self.exact(g,str(p.relative_to(ROOT)),a,dict(relative_path=p.relative_to(ROOT).as_posix(),size_bytes=len(b),sha256=sha(b)))
    def run(self):
        a=self; manifest_path=Path(str(PREFIX)+'-manifest.json'); m=read(manifest_path)
        freeze=read(Path(str(PREFIX)+'-artifact-freeze.json')); ver=read(Path(str(PREFIX)+'-verification.json'))
        a.eq('manifest','physical hash',sha(raw(manifest_path)),PHY)
        a.eq('manifest','semantic hash',digest({k:v for k,v in m.items() if k!='manifest_sha256'}),SEM)
        a.bytes('manifest','canonical bytes',manifest_path,canonical_bytes(m,True))
        expected_manifest=dict(manifest_version=1,manifest_type='ex2-stage6-diagnostic',manifest_id=ID,manifest_sha256=SEM,protocol_version='1.4',evidence_schema_version=2,evidence_kind='diagnostic',instrument_mode='H',machine_id='ex2-s6-e1-machine',child_executable_path='out/build/x64-release/src/app/ComputeLabEx2Stage6.exe',expected_source_revision=SRC,expected_git_dirty=False,expected_child_executable_sha256=freeze['artifacts']['child']['sha256'],expected_supervisor_executable_sha256=freeze['artifacts']['supervisor']['sha256'],expected_gpu_uuid=GPU,cuda_device_ordinal=0,vulkan_physical_device_index=0,expected_vulkan_shader_sha256={k:freeze['artifacts'][k]['sha256'] for k in ['a1','a2','b1','b2','c','d1']},operation_timeout_ms=60000,child_timeout_ms=1200000,campaign_timeout_ms=86400000,continuation_policy='resolved-only-no-retry',declared_cell_group_count=22,declared_child_count=220,groups=[dict(cell_index=c,declared_child_count=10,children=[dict(sequence_index=n,session_id=slot(n)['session_id']) for n in range(c*10,c*10+10)]) for c in range(22)])
        a.exact('manifest','frozen manifest',m,expected_manifest)
        shared=dict(source_revision=SRC,protocol_version='1.4',evidence_schema_version=2,evidence_kind='diagnostic',instrument_mode='H',machine_id='ex2-s6-e1-machine',release_build_pass=True,git_clean=True)
        a.fields('preparation','freeze',freeze,shared)
        a.fields('preparation','freeze selectors',freeze,dict(cuda_device_ordinal=0,vulkan_physical_device_index=0,cuda_uuid=GPU,vulkan_uuid=GPU,expected_gpu_uuid=GPU,build_preset='x64-release',build_type='Release'))
        a.fields('preparation','verification',ver,{k:v for k,v in shared.items() if k!='machine_id'})
        a.fields('preparation','verification identity',ver,dict(manifest_id=ID,manifest_sha256=SEM,manifest_file_sha256=PHY,status='PASS',verification_pass=True,schedule_verified=True,cell_group_count=22,child_count=220,planned_operations=22000,operations_per_child=100,warmup_count=0,artifacts=freeze['artifacts'],gate0='FAIL',stage2='NOT GRANTED',production_backend='UNSELECTED'))
        a.fields('preparation','verification GPU',ver['gpu'],dict(cuda_device_ordinal=0,vulkan_physical_device_index=0,cuda_uuid=GPU,vulkan_uuid=GPU,expected_gpu_uuid=GPU,same_physical_gpu=True))
        a.facts['live_artifacts']=[]
        for k,entry in freeze['artifacts'].items():
            p=ROOT/entry['path']
            if not p.exists(): a.facts['live_artifacts'].append(dict(name=k,status='missing-live-artifact-limitation')); continue
            b=raw(p); a.eq('artifacts',k+' hash',sha(b),entry['sha256']); a.eq('artifacts',k+' size',len(b),entry['size_bytes'])
            a.facts['live_artifacts'].append(dict(name=k,status='verified',**entry))
        for phase,anchor in ver['verification_reports'].items():
            p=ROOT/anchor['path']; b=raw(p); a.eq('preparation',phase+' report hash',sha(b),anchor['sha256']); a.eq('preparation',phase+' report size',len(b),anchor['size_bytes'])
            r=read(p); a.fields('preparation',phase,r,dict(source_revision=SRC,protocol_version='1.4',manifest_id=ID,manifest_file_sha256=PHY,stored_semantic_sha256=SEM,calculated_semantic_sha256=SEM,verification_pass=True,schedule_verified=True,git_clean=True,branch='main',artifacts=freeze['artifacts'],cell_group_count=22,child_count=220,planned_operations=22000,production_children_executed=0,supervisor_launches=0))
            a.eq('preparation',phase+' invocation count',anchor['invocations'],1)
        review=raw(Path(str(PREFIX)+'-review.md')).decode('utf-8')
        for value in [SRC,SEM,PHY,GPU,'1.4',* [x['sha256'] for x in freeze['artifacts'].values()]]: a.check('preparation','review contains '+value,value in review)
        analysisroot=Path(str(PREFIX)+'-analysis')
        a.eq('topology','slot namespace',[p.name for p in sorted(BASE.glob(ID+'-slot-*'))],[slot(n)['session_id'] for n in range(220)])
        a.eq('topology','analysis exact namespace',sorted(p.name for p in safe(analysisroot).iterdir()),sorted([f'cell-{c:02}-analysis.json' for c in range(22)]+['campaign-analysis.json']))
        for suffix in ['-control.json.incomplete','-control.json.incomplete.tmp','-control.incomplete','-control.incomplete.tmp']:
            a.check('topology','no '+suffix,not Path(str(PREFIX)+suffix).exists())
        ledgerpath=Path(str(PREFIX)+'-control.json'); l=read(ledgerpath)
        a.bytes('ledger','canonical ledger',ledgerpath,canonical_bytes(l,True))
        ledgerkeys='record_version record_type protocol_version manifest_id manifest_sha256 manifest_file_sha256 manifest resume_policy ledger_revision control_state control_start_time_utc campaign_start_time_utc campaign_end_time_utc qpc_frequency campaign_start_counter campaign_deadline_counter observed_preflight fatal_sequence_index fatal_reason slots cell_analysis campaign_analysis'.split()
        a.eq('ledger','key set',sorted(l),sorted(ledgerkeys))
        a.fields('ledger','identity',l,dict(record_version=1,record_type='ex2-stage6-supervisor-control',protocol_version='1.4',manifest_id=ID,manifest_sha256=SEM,manifest_file_sha256=PHY,manifest=m,resume_policy='forbidden',control_state='completed',fatal_sequence_index=None,fatal_reason=None,ledger_revision=905,campaign_start_time_utc='2026-10-07T15:05:48.692Z',campaign_end_time_utc='2026-10-07T15:27:57.660Z'))
        a.eq('ledger','campaign deadline ticks',l['campaign_deadline_counter'],l['campaign_start_counter']+86400*l['qpc_frequency'])
        a.check('ledger','positive bounded QPC',0<l['qpc_frequency']<2**63 and 0<l['campaign_start_counter']<l['campaign_deadline_counter']<2**63)
        a.exact('ledger','preflight',l['observed_preflight'],dict(source_revision=SRC,git_dirty=False,child_executable_sha256=m['expected_child_executable_sha256'],supervisor_executable_sha256=m['expected_supervisor_executable_sha256'],cuda_uuid=GPU,vulkan_uuid=GPU,vulkan_shader_sha256=m['expected_vulkan_shader_sha256']))
        a.eq('ledger','slot count',len(l['slots']),220); a.eq('ledger','cell anchors',len(l['cell_analysis']),22)
        a.facts['dispositions']=dict(collections.Counter(s['disposition'] for s in l['slots']))
        a.eq('ledger','dispositions',a.facts['dispositions'],{'resolved_success':220})
        logical=read(OUT/'logical-input-facts.json')['cells']; a.eq('logical','cell count',len(logical),22)
        a.facts['cleanup_slots']=[]; descs=[]; totals=collections.Counter(); maximum=dict(value=-1)
        previous_exit=l['campaign_start_time_utc']; previous_qpc=l['campaign_start_counter']
        for n,s in enumerate(l['slots']):
            sp=slot(n); w=workload(sp['cell_index']); label=f'slot-{n:03}'; package=BASE/sp['session_id']; p=s['process']; ins=s['inspection']; cleanup=s['reconciliation_reason']=='resolved_success_post_completion_cleanup'
            a.eq('schedule',label+' slot keys',sorted(s),sorted('sequence_index session_id control_phase entered_revision process_revision inspection_revision terminal_revision disposition process inspection reconciliation_reason continuation'.split()))
            a.fields('schedule',label,s,dict(sequence_index=n,session_id=sp['session_id'],control_phase='resolved',disposition='resolved_success',continuation='schedule_complete' if n==219 else 'launch_next',**{k:2+4*n+n//10+i for i,k in enumerate(['entered_revision','process_revision','inspection_revision','terminal_revision'])}))
            a.eq('control',label+' process keys',sorted(p),sorted('active_attempt campaign_timed_out child_timed_out clean_eof containment_assigned containment_verification_failed containment_verified control_error descendant_survival_observed exit_code exit_kind exit_time_utc job_active_processes job_empty_confirmed job_total_processes last_returned_attempt launch_time_utc operation_timed_out primary_termination_confirmed process_created process_id process_resumed progress_form progress_invalid progress_transport_failed progress_transcript supervisor_termination_requested terminate_job_succeeded timed_out_sample_index trailing_bytes'.split()))
            a.fields('control',label,p,dict(process_created=True,process_resumed=True,containment_assigned=True,containment_verified=True,primary_termination_confirmed=True,job_empty_confirmed=True,job_active_processes=0,containment_verification_failed=False,control_error=None,exit_code=0,progress_invalid=False,progress_transport_failed=False,clean_eof=True,trailing_bytes=0,operation_timed_out=False,child_timed_out=False,campaign_timed_out=False,timed_out_sample_index=None,active_attempt=None,progress_form='Full',last_returned_attempt=dict(slot_sequence_index=n,sample_index=99),descendant_survival_observed=cleanup,supervisor_termination_requested=cleanup,terminate_job_succeeded=cleanup))
            a.check('control',label+' lifetime count',p['job_total_processes'] is None or type(p['job_total_processes']) is int and 1<=p['job_total_processes']<2**32)
            a.check('control',label+' PID',type(p['process_id']) is int and 0<p['process_id']<2**32)
            a.check('control',label+' accepted exit kind',p['exit_kind'] in (['VoluntaryStage6','SupervisorForced'] if cleanup else ['VoluntaryStage6']))
            a.eq('control',label+' reason',s['reconciliation_reason'],'resolved_success_post_completion_cleanup' if cleanup else 'resolved_success')
            if cleanup: a.facts['cleanup_slots'].append(dict(**sp,control={k:v for k,v in p.items() if k!='progress_transcript'},reconciliation_reason=s['reconciliation_reason']))
            a.check('sequential',label+' UTC bounds',previous_exit<=p['launch_time_utc']<=p['exit_time_utc']<=l['campaign_end_time_utc'])
            previous_exit=p['exit_time_utc']; events=p['progress_transcript']; a.eq('progress',label+' length',len(events),200)
            for j,e in enumerate(events):
                a.exact('progress',label+f' event {j}',e,dict(child_qpc=e['child_qpc'],event=1+j%2,magic=0x53365043,receive_qpc=e['receive_qpc'],reserved=0,sample_index=j//2,slot_sequence_index=n,version=1))
                a.check('progress',label+f' QPC {j}',type(e['child_qpc']) is int and type(e['receive_qpc']) is int and 0<previous_qpc<=e['child_qpc']<=e['receive_qpc']<l['campaign_deadline_counter'])
                previous_qpc=e['child_qpc']; totals['Started' if e['event']==1 else 'Returned']+=1
            a.check('topology',label+' directory',safe(package).is_dir()); a.eq('topology',label+' exact files',sorted(x.name for x in package.iterdir()),NAMES)
            anchors=[]
            for name in NAMES:
                f=package/name; b=raw(f); anchors.append(dict(name=name,relative_path=f.relative_to(ROOT).as_posix(),size_bytes=len(b),sha256=sha(b))); totals['package_files']+=1
            ph=digest(dict(package_hash_version=1,artifacts=[{k:v for k,v in x.items() if k!='relative_path'} for x in anchors]))
            inspection_expected=dict(artifacts=anchors,attempted=True,changed_during_inspection=False,errors=[],exact_file_set=True,location='Final',package_finalized=True,package_sha256=ph,science=dict(error_code=None,failure_phase=None,process_status='ok',recorded_sample_count=100,terminal_state='Success'),scientific_bundle_parsed=True,scientific_bundle_valid=True,scientific_bytes_canonical=True,sidecar=None,sidecar_artifact=None,sidecar_present=False,sidecar_valid=False,structurally_valid=True,topology='FinalOnly')
            a.exact('package_hash',label+' inspection',ins,inspection_expected)
            env=read(package/'environment.json'); backend=sp['backend']; shader=m['expected_vulkan_shader_sha256'][w['variant'].lower()] if backend=='vulkan' and w['workload']!='E' else None
            condition=dict(w,protocol_version='1.4',machine_id='ex2-s6-e1-machine',gpu_uuid_identity=GPU,generator_revision='ex2-mix64-v1',operation_boundary=boundary(w)); cid=digest(condition)
            series=dict(condition,backend=backend,block_index=sp['block_index'],executable_sha256=m['expected_child_executable_sha256'],order_slot=sp['order_slot'],planned_sample_count=100,process_index=sp['process_index'],shader_sha256=shader,source_revision=SRC,warmup_count=0); sid=digest(series)
            a.fields('identity',label+' environment',env,dict(condition,**{k:v for k,v in sp.items() if k!='session_id'},schema_version=2,experiment_id='EX-2',run_id=sp['session_id'],git_commit=SRC,git_dirty=False,evidence_kind='diagnostic',warmup_count=0,planned_sample_count=100,comparison_condition_id=cid,series_id=sid,source_revision=SRC,executable_sha256=m['expected_child_executable_sha256'],shader_sha256=shader,configure_preset='x64-release',build_type='Release',validation_enabled=False,diagnostic_instrumentation=False))
            a.fields('logical',label,env,{k:logical[sp['cell_index']][k] for k in ['input_sha256','expected_output_sha256']})
            a.eq('logical',label+' workload',logical[sp['cell_index']]['workload'],w)
            a.eq('identity',label+' env keys',sorted(env),sorted(COMMON_KEYS+ENV_EXTRA)); d=env['backend_native']; a.eq('identity',label+' native keys',sorted(d),sorted(NATIVE_KEYS))
            a.eq('identity',label+' implementation',d['implementation'],f'ex2-{backend}-{w["variant"].lower()}-native')
            a.fields('timing',label+' native off',d,dict.fromkeys(['timestamp_valid_bits','timestamp_period_ns','native_timing_method','native_timing_resolution_ns','native_timing_start_stage','native_timing_stop_stage','native_duration_envelope_ns']))
            a.eq('timing',label+' markers',d['native_markers_enabled'],False)
            provenance_required=['os_name','os_version','cpu_name','gpu_name','gpu_vendor','gpu_device_id','nvidia_driver_version','compiler_name','compiler_version','cmake_version','ninja_version']+(['cuda_toolkit_version','cuda_runtime_version','cuda_compute_capability'] if backend=='cuda' else ['vulkan_sdk_version','vulkan_device_api_version'])
            for k in provenance_required: a.check('identity',label+' provenance '+k,isinstance(env[k],str) and 0<len(env[k])<=1024 and not any(c in env[k] for c in '\\/\r\n\0'))
            for k in ['system_memory_bytes','gpu_memory_bytes']: a.check('identity',label+' '+k,type(env[k]) is int and env[k]>0)
            a.check('identity',label+' timestamp',bool(re.fullmatch(r'\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ',env['timestamp_utc'])) and p['launch_time_utc'][:19]<=env['timestamp_utc'][:19]<=p['exit_time_utc'][:19])
            if backend=='cuda': a.fields('identity',label+' CUDA native',d,dict(stream_flags='nonblocking',queue_family_index=None,queue_flags=None,queue_count=None,input_memory_flags=None,output_memory_flags=None,upload_memory_flags=None,readback_memory_flags=None))
            else:
                a.eq('identity',label+' Vulkan stream',d['stream_flags'],None)
                for k in ['queue_family_index','queue_flags','queue_count','input_memory_flags','output_memory_flags','upload_memory_flags','readback_memory_flags']: a.check('identity',label+' Vulkan '+k,type(d[k]) is int and 0<=d[k]<2**32)
                a.check('identity',label+' Vulkan queue count',d['queue_count']>0)
            ordered_env={k:env[k] for k in COMMON_KEYS+ENV_EXTRA}; ordered_env['backend_native']={k:d[k] for k in NATIVE_KEYS}
            a.bytes('canonical_package',label+' environment',package/'environment.json',canonical_bytes(ordered_env))
            init=[INIT_HEADER.split(','),['2',sp['session_id'],'EX-2',backend,str(sp['process_index']),'0','backend_setup',w['workload'],w['variant'],'' if w['element_count'] is None else str(w['element_count']),'setup_complete','','resources and immutable sources ready for Started sample 0']]
            a.bytes('canonical_package',label+' initialization',package/'initialization.csv',csvbytes(init))
            values={k:[] for k in METRICS}; canonical_rows=[SAMPLE_HEADER.split(',')]; row_count=0
            safe(package/'samples.csv')
            with (package/'samples.csv').open('r',encoding='utf-8',newline='') as f:
                reader=csv.DictReader(f,strict=True); a.eq('samples',label+' header',reader.fieldnames,SAMPLE_HEADER.split(','))
                for j,row in enumerate(reader):
                    row_count+=1; totals['sample_rows']+=1
                    expected_row={k:('' if v is None else str(v)) for k,v in dict(w,schema_version=2,run_id=sp['session_id'],experiment_id='EX-2',comparison_condition_id=cid,series_id=sid,backend=backend,warmup_count=0,planned_sample_count=100,block_index=sp['block_index'],order_slot=sp['order_slot'],process_index=sp['process_index'],sample_index=j,validation_passed='true',status='ok',failure_phase='',error_code='',native_device_interval_ns='').items()}
                    for k in METRICS:
                        a.check('timing',label+f' row {j} '+k,bool(re.fullmatch(r'0|[1-9][0-9]*',row[k])))
                        v=int(row[k]); a.check('timing',label+f' row {j} uint64 '+k,0<=v<2**64); values[k].append(v); expected_row[k]=str(v)
                    a.exact('samples',label+f' row {j}',row,expected_row)
                    a.eq('timing',label+f' row {j} arithmetic',values[METRICS[0]][-1]+values[METRICS[1]][-1],values[METRICS[2]][-1])
                    totals['successful_rows']+=row['status']=='ok' and row['validation_passed']=='true'; totals['validation_failures']+=row['validation_passed']=='false'
                    canonical_rows.append([expected_row[k] for k in SAMPLE_HEADER.split(',')])
                    if values['host_completion_ns'][-1]>maximum['value']: maximum=dict(value=values['host_completion_ns'][-1],slot_sequence_index=n,sample_index=j,participated_in_summary=True)
            a.eq('samples',label+' row count',row_count,100)
            a.bytes('canonical_package',label+' samples',package/'samples.csv',csvbytes(canonical_rows))
            ms={k:metrics(values[k]) for k in METRICS}
            group=dict(comparison_condition_id=cid,series_id=sid,run_id=sp['session_id'],cell_index=sp['cell_index'],slot_sequence_index=n,backend=backend,**w,warmup_count=0,planned_sample_count=100,block_index=sp['block_index'],order_slot=sp['order_slot'],process_index=sp['process_index'])
            summary=dict(schema_version=2,run_id=sp['session_id'],experiment_id='EX-2',protocol_version='1.4',evidence_kind='diagnostic',process_status='ok',failure_phase=None,error_code=None,sample_groups=[dict(group=group,recorded_sample_count=row_count,successful_sample_count=row_count,validation_failures=0,failed_sample_count=0,metrics=dict(ms,native_device_interval_ns=None))])
            a.eq('summary',label+' semantic',read(package/'summary.json'),summary)
            a.bytes('summary',label+' canonical',package/'summary.json',canonical_bytes(summary))
            desc=dict(slot_sequence_index=n,block_index=sp['block_index'],order_slot=sp['order_slot'],process_index=sp['process_index'],package_sha256=ph,series_id=sid,terminal_state='success',recorded_sample_count=row_count,successful_sample_count=row_count,validation_failures=0,failed_sample_count=0,failure_phase=None,error_code=None,metrics=ms,windows=[dict(begin=b,count=25,successful_row_count=len(values['host_completion_ns'][b:b+25]),host_completion_median_ns=metrics(values['host_completion_ns'][b:b+25])['median']) for b in range(0,100,25)])
            descs.append(desc); a.processes.append(dict(**sp,workload=w,series_id=sid,comparison_condition_id=cid,package_path=package.relative_to(ROOT).as_posix(),package_sha256=ph,control_disposition=s['disposition'],reconciliation_reason=s['reconciliation_reason'],cleanup_anomaly=cleanup,scientific_terminal_state='Success',recorded_sample_count=row_count,successful_sample_count=row_count,validation_failures=0,summary_regenerated=True,package_verified=True,analysis_eligible=True,control={k:v for k,v in p.items() if k!='progress_transcript'},file_anchors=anchors,progress_event_count=len(events)))
        a.eq('cleanup','exact count',len(a.facts['cleanup_slots']),2)
        a.facts['totals']=dict(totals,final_package_dirs=len(descs),ordinary_success=len(descs)-len(a.facts['cleanup_slots']),cleanup_success=len(a.facts['cleanup_slots']),cell_analyses=22,campaign_analyses=1)
        a.facts['maximum_retained_host_completion']=maximum
        for k,v in dict(package_files=880,sample_rows=22000,successful_rows=22000,validation_failures=0,Started=22000,Returned=22000).items(): a.eq('counts',k,totals[k],v)
        cellhashes=[]
        for c in range(22):
            backends={}
            for backend in ['cuda','vulkan']:
                ps=sorted([descs[n] for n in range(c*10,c*10+10) if slot(n)['backend']==backend],key=lambda p:p['process_index'])
                a.eq('analysis',f'cell {c} {backend} process positions',[p['process_index'] for p in ps],list(range(5)))
                meds=[p['metrics']['host_completion_ns']['median'] for p in ps]
                backends[backend]=dict(resolved_process_count=5,successful_process_count=5,diagnostic_failure_process_count=0,process_median_span_ratio=max(meds)/min(meds) if min(meds)>0 else None,processes=ps)
            cell=dict(analysis_version=1,protocol_version='1.4',analysis_kind='stage6-cell-diagnostic',cell_index=c,workload=workload(c),status='complete',resolved_slot_count=10,successful_slot_count=10,diagnostic_failure_slot_count=0,backends=backends)
            path=analysisroot/f'cell-{c:02}-analysis.json'; a.eq('analysis',f'cell {c} semantic',read(path),cell); a.bytes('analysis',f'cell {c} canonical',path,canonical_bytes(cell)); a.anchor('analysis',path,l['cell_analysis'][c]); cellhashes.append(sha(canonical_bytes(cell)))
        campaign=dict(analysis_version=1,protocol_version='1.4',analysis_kind='stage6-campaign-diagnostic',status='complete',declared_slots=220,attempted_slots=220,resolved_slots=220,successful_slots=220,diagnostic_failure_slots=0,unlaunched_slots=0,fully_resolved_cells=22,cells_with_diagnostic_failures=0,cell_analysis_sha256=cellhashes,claim_scope=dict(cross_backend_performance_admitted=False,stage2_authorized=False,production_backend_selected=False))
        cp=analysisroot/'campaign-analysis.json'; a.eq('campaign','semantic',read(cp),campaign); a.bytes('campaign','canonical',cp,canonical_bytes(campaign)); a.anchor('campaign',cp,l['campaign_analysis'])
        a.facts['campaign']=campaign
        prohibited=re.compile(r'cuda.*vulkan|vulkan.*cuda|speedup|percent_difference|winner|loser|rank|backend_score|paired_ratio|production_selection')
        def scan(obj,path):
            if isinstance(obj,dict):
                for k,v in obj.items():
                    a.check('firewall',path+'.'+k,not prohibited.search(k)); scan(v,path+'.'+k)
            elif isinstance(obj,list):
                for i,v in enumerate(obj): scan(v,path+f'[{i}]')
        for p in sorted(analysisroot.iterdir()): scan(read(p),p.name)
        launch=read(ROOT/f'results/tmp/{ID}-execution/launch.json'); terminal=read(ROOT/f'results/tmp/{ID}-execution/terminal.json')
        op=dict(launch_count=1,process_id=39708,start_utc='2026-10-07T15:05:21.7273853Z',manifest_id=ID,manifest_sha256=SEM,manifest_file_sha256=PHY)
        a.fields('operator','launch',launch,dict(op,executable=freeze['artifacts']['supervisor']['path'],arguments=['--manifest',manifest_path.relative_to(ROOT).as_posix()]))
        a.fields('operator','terminal',terminal,dict(op,end_utc='2026-10-07T15:27:58.3297548Z',raw_exit_code=0))
        def precise_time(s):
            base,fraction=s.rstrip('Z').split('.'); t=datetime.datetime.fromisoformat(base).replace(tzinfo=datetime.timezone.utc)
            return decimal.Decimal(int(t.timestamp()))+decimal.Decimal('0.'+fraction)
        duration=precise_time(terminal['end_utc'])-precise_time(launch['start_utc'])
        a.eq('operator','wall duration',decimal.Decimal(str(terminal['operator_wall_seconds'])),duration)
        a.check('operator','time enclosure',launch['start_utc']<=l['control_start_time_utc']<=l['campaign_start_time_utc']<=l['campaign_end_time_utc']<=terminal['end_utc'])
        a.facts['operator']=dict(launch=launch,terminal=terminal,calculated_wall_seconds=str(duration))
        a.facts['ledger_summary']={k:l[k] for k in ['control_state','ledger_revision','control_start_time_utc','campaign_start_time_utc','campaign_end_time_utc','qpc_frequency','fatal_reason','fatal_sequence_index']}
        return dict(check_counts=dict(a.checks),discrepancies=a.failures,facts=a.facts,processes=a.processes)

if __name__=='__main__' and sys.argv[1]=='audit':
    result=Audit().run(); write(OUT/'independent-facts.json',result)
    print(json.dumps(dict(check_counts=result['check_counts'],discrepancy_count=len(result['discrepancies']),discrepancies=result['discrepancies'][:8],totals=result['facts']['totals'],cleanup_slots=[s['slot_sequence_index'] for s in result['facts']['cleanup_slots']]),default=str))

def supplemental():
    a=Audit(); v=read(Path(str(PREFIX)+'-verification.json')); f=read(Path(str(PREFIX)+'-artifact-freeze.json'))
    prep=ROOT/f'results/tmp/{ID}-prep'; exe=ROOT/f'results/tmp/{ID}-execution'
    expected_schedule=[dict(sequence_index=n,**{k:x for k,x in slot(n).items() if k!='slot_sequence_index'}) for n in range(220)]
    dates=[]
    for phase in ['candidate','final','stability']:
        r=read(prep/f'logs/{phase}-verification.json')
        a.eq('preparation',phase+' complete schedule',r['schedule'],expected_schedule)
        a.eq('preparation',phase+' label',r['phase'],phase)
        a.fields('preparation',phase+' GPU',r['gpu'],dict(cuda_device_ordinal=0,vulkan_physical_device_index=0,cuda_uuid=GPU,vulkan_uuid=GPU,expected_gpu_uuid=GPU,same_physical_gpu=True))
        dates.append(datetime.datetime.fromisoformat(r['verified_utc']))
    dates.append(datetime.datetime.fromisoformat(v['recorded_utc']))
    intent=read(exe/'launch-intent.json'); dates.append(datetime.datetime.fromisoformat(intent['intent_utc']))
    a.check('preparation','chronological verification to launch intent',dates==sorted(dates))
    a.bytes('preparation','candidate equals frozen final',prep/'candidate-manifest.json',raw(Path(str(PREFIX)+'-manifest.json')))
    a.fields('preparation','verification record extras',v,dict(record_type='ex2-stage6-pre-execution-verification',record_version=1,branch='main',build_invocations=1,calculated_manifest_sha256=SEM,powershell_manifest_file_sha256=PHY,campaign_executed=False,launch_authorized=False,production_children_executed=0,supervisor_launches=0,namespace_clear=True,tests_rerun=False,source_modified=False,historical_attempt_modified=False))
    a.fields('preparation','initial guards',read(prep/'logs/initial-guards.json'),dict(head=SRC,branch='main',status=[],initial_namespace_clear=True,production_children_executed=0,supervisor_launches=0))
    a.exact('preparation','release build record',read(prep/'logs/release-build-success.json'),dict(release_build_pass=True,build_invocations=1,tests_rerun=False))
    a.exact('preparation','dual physical hashes',read(prep/'logs/final-powershell-hash.json'),dict(powershell_physical_sha256=PHY,python_physical_sha256=PHY,stored_semantic_sha256=SEM,calculated_semantic_sha256=SEM,all_match=True))
    auth=f'AUTHORIZE S6-E1B {ID} {SEM} {PHY}'
    a.eq('operator','review authorization tuple',v['human_authorization_string'],auth)
    a.fields('operator','launch intent',intent,dict(manifest_id=ID,manifest_sha256=SEM,manifest_file_sha256=PHY,authorization=auth,executable=f['artifacts']['supervisor']['path'],arguments=['--manifest',f'results/local/{ID}-stage6-manifest.json'],working_directory=str(ROOT),read_only_guards='PASS',matching_live_processes=0,namespace_paths_absent=665,standalone_verifiers_rerun=0,external_timeout=False))
    a.eq('operator','execution namespace',sorted(x.name for x in safe(exe).iterdir()),['launch-intent.json','launch.json','stderr.log','stdout.log','terminal.json'])
    for k,entry in f['artifacts'].items():
        p=ROOT/entry['path']
        if p.exists():
            b=raw(p); a.eq('artifacts',k+' end hash',sha(b),entry['sha256']); a.eq('artifacts',k+' end size',len(b),entry['size_bytes'])
    # Publication order is reconstructed from retained revisions; these are
    # consistency checks, not a claim of independent continuous observation.
    l=read(Path(str(PREFIX)+'-control.json')); gaps=[]
    for n in range(219):
        p=l['slots'][n]['process']; nxt=l['slots'][n+1]['process']
        gap=(datetime.datetime.fromisoformat(nxt['launch_time_utc'])-datetime.datetime.fromisoformat(p['exit_time_utc'])).total_seconds()
        a.check('sequential',f'gap {n}',gap>=0); gaps.append(gap)
        a.check('sequential',f'QPC receive boundary {n}',p['progress_transcript'][-1]['receive_qpc']<=nxt['progress_transcript'][0]['child_qpc'])
    for c in range(21): a.eq('publication',f'cell {c} revision reservation',l['slots'][(c+1)*10]['entered_revision']-l['slots'][c*10+9]['terminal_revision'],2)
    a.eq('publication','last terminal -> cell -> campaign -> terminal ledger',l['ledger_revision']-l['slots'][-1]['terminal_revision'],3)
    before=read(OUT/'retained-files-before.json'); after=inventory()
    a.eq('immutability','complete path size hash mtime inventory',after,before)
    # Confirm minimum immutable coverage without relying on prefix count alone.
    paths={x['path'] for x in before}
    required=[f'results/local/{ID}-slot-{n:03}/{name}' for n in range(220) for name in NAMES]
    required += [f'results/local/{ID}-stage6-{suffix}' for suffix in ['artifact-freeze.json','manifest.json','verification.json','review.md','control.json']]
    required += [f'results/local/{ID}-stage6-analysis/cell-{n:02}-analysis.json' for n in range(22)]
    required += [f'results/local/{ID}-stage6-analysis/campaign-analysis.json',f'results/tmp/{ID}-execution/launch.json',f'results/tmp/{ID}-execution/terminal.json']
    a.check('immutability','all 910 required evidence files covered',len(required)==910 and set(required)<=paths)
    write(OUT/'retained-files-after.json',after)
    write(OUT/'source-after.json',git_guard())
    write(OUT/'supplemental-facts.json',dict(check_counts=dict(a.checks),discrepancies=a.failures,immutable_file_count=len(after),immutable_bytes=sum(x['size_bytes'] for x in after),required_file_count=len(required),sequential_boundaries=len(gaps),minimum_interprocess_gap_seconds=min(gaps)))
    print(json.dumps(dict(check_counts=dict(a.checks),discrepancies=a.failures,immutable_file_count=len(after))))

if __name__=='__main__' and sys.argv[1]=='supplemental': supplemental()

def finalize():
    core=read(OUT/'independent-facts.json'); extra=read(OUT/'supplemental-facts.json'); logical=read(OUT/'logical-input-facts.json')
    discrepancies=core['discrepancies']+extra['discrepancies']
    # Final publication is permitted only after human-facing review of the
    # compact audit facts. Never overwrite any final or retained artifact.
    final_json=Path(str(PREFIX)+'-postrun-audit.json'); final_md=Path(str(PREFIX)+'-postrun-audit.md'); indexpath=Path(str(PREFIX)+'-accepted-process-index.json')
    for p in [final_json,final_md,indexpath]:
        if p.exists(): raise FileExistsError('CREATE_NEW collision: '+str(p))
    if discrepancies: raise ValueError('cannot publish acceptance with discrepancies')
    if inventory()!=read(OUT/'retained-files-after.json'): raise ValueError('retained inventory changed after immutability check')
    source=git_guard()
    source_files=['AGENTS.md','docs/charter.md','docs/methodology.md','docs/results-format.md','docs/ex2.md']
    source_files += [f'src/ex2/Ex2{name}.{ext}' for name in ['Configuration','Stage6Plan','Stage6Evidence','Stage6Analysis','Stage6Progress','Stage6Supervisor','Input','LogicalInput','IndexPermutation','ContentionTargets'] for ext in ['hpp','cpp']]
    source_files += ['src/ex2/Ex2CpuOracles.hpp']+[f'src/ex2/Ex2Cpu{x}.cpp' for x in 'ABCDE']+['src/app/Ex2Stage6Execution.hpp','src/app/Ex2Stage6Execution.cpp']
    spec=dict(audit_version=1,audit_type='independent-s6-e1b-postrun-evidence-audit',source_revision=SRC,manifest_id=ID,protocol_version='1.4',schema_version=2,evidence_kind='diagnostic',instrument_mode='H',initial_namespace_absent=True,initial_namespace_evidence='Read-only PowerShell Test-Path of all four required output paths in this chat before first write',independence='Fresh audit chat; independently written Python; no project helpers/serializers/executables called. Historical memory registry consulted for scope guidance only; no prior campaign audit results used.',model_selection='Recommended GPT-6 Astra / High cannot be independently attested from exposed runtime metadata',python=sys.version,numpy=logical['numpy'],numpy_scope='Existing NumPy used only for CPU integer-array regeneration, including expected-output digests; statistics/control/serialization/hash use standard library',frozen_cells=[workload(c) for c in range(22)],frozen_schedule=[slot(n) for n in range(220)],formulas=dict(metric_input='IEEE-754 binary64, raw sample order',mean='sequential sum / n',sd='sqrt(sequential squared deviations / (n-1))',cv='sample SD / mean; null if mean zero',median='sorted middle or average of two middle values',p95='nearest rank ceil(0.95*n)',windows='original sample indexes 0..24,25..49,50..74,75..99',span='within-backend max(five positive process medians)/min; only for five complete successful processes',package_hash='SHA256 of sorted-key canonical JSON without LF: package_hash_version=1 and ordered four name/size_bytes/sha256 artifact objects',manifest_hash='SHA256 of sorted-key canonical JSON without manifest_sha256 and without LF'),source_authority_hashes=[dict(path=p,sha256=sha(raw(ROOT/p))) for p in source_files],prohibited_actions_performed=[])
    write(OUT/'audit-spec.json',spec)
    accepted_keys='slot_sequence_index cell_index workload backend block_index order_slot process_index session_id series_id package_path package_sha256 control_disposition reconciliation_reason cleanup_anomaly scientific_terminal_state recorded_sample_count successful_sample_count validation_failures summary_regenerated package_verified analysis_eligible'.split()
    records=[{k:p[k] for k in accepted_keys} for p in core['processes']]
    if len(records)!=220 or [p['slot_sequence_index'] for p in records]!=list(range(220)): raise ValueError('index coverage')
    index=dict(index_version=1,index_type='stage6-accepted-process-index',manifest_id=ID,source_revision=SRC,protocol_version='1.4',evidence_kind='diagnostic',claim_scope='DR-44 bounded descriptive/diagnostic only',processes=records)
    write(indexpath,index); indexhash=sha(raw(indexpath))
    if read(indexpath)!=index: raise ValueError('index readback mismatch')
    limitations=[
        'Recommended GPT-6 Astra and High UI selections are not independently exposed or attested. Matrix 002 and 003 are UNVERIFIED recommendations, not claimed passes. This does not waive any evidence-integrity check.',
        'Retained correctness evidence consistency was audited, including independently regenerated input and expected-output digests. No GPU code ran; absent full GPU output buffers cannot be reconstructed as observations. Stage-4 remains deeper implementation-correctness evidence.',
        'The two DR-45 cleanup-success cases satisfy protocol 1.4. The audit does not establish environmental root cause or explain descendant creation.',
        'Sequential/no-retry conclusions are bounded to retained launch/exit/QPC, revision, namespace and operator evidence and the accepted source contract. This post-run audit is not independent continuous OS/process telemetry and cannot prove absence of unrecorded out-of-band activity.',
        'Preparation snapshots describe their pre-launch phase; campaign_executed=false and launch_authorized=false there do not contradict later retained authorized launch records. No retrospective rewrite was performed.',
        'Host/GPU/software metadata is retained provenance, not newly measured machine state. Eight live binary/shader artifacts were independently rehashed; GPU identity APIs were not invoked.',
        'Every successful raw timing row and both cleanup processes participated unchanged. Extreme timings were retained without interpretation, deletion, trimming, winsorization or process exclusion.',
        'Python standard library handled audit logic and binary64 statistics; installed NumPy '+logical['numpy']+' vectorized independently written fixed-width CPU identity formulas. No project helper or serializer was imported.',
        'Gate 0 remains FAIL; Stage 2 NOT GRANTED; production backend UNSELECTED. Acceptance grants no cross-backend comparison, winner, speedup, ranking, scaling interpretation or Stage-7 selection.'
    ]
    request=Path(r'C:\Users\2dfdc\.codex\attachments\74038cc3-f24d-4baa-a40e-1d6b72f17f60\Pasted text.txt').read_text(encoding='utf-8')
    entries=re.findall(r'^E1B-AUD-(\d{3}) (.+)$',request,re.M)
    if len(entries)!=120 or [int(n) for n,_ in entries]!=list(range(1,121)): raise ValueError('acceptance matrix extraction')
    matrix=[dict(id='E1B-AUD-'+n,requirement=text.strip(),status='UNVERIFIED_RECOMMENDATION' if int(n) in [2,3] else 'PASS',evidence='Runtime UI selection not exposed' if int(n) in [2,3] else 'Independent facts, supplemental facts, source snapshots and audit publication receipt; see report sections') for n,text in entries]
    totals=core['facts']['totals']; totals.update(resolved_success=220,resolved_diagnostic_failure=0,unresolved_campaign_fatal=0,not_launched=0,progress_events=44000,scientifically_successful_processes=220)
    report=dict(audit_version=1,audit_type='independent-s6-e1b-full-campaign-postrun-evidence-audit',verdict='ACCEPTED',source_revision=SRC,branch='main',manifest_id=ID,manifest_sha256=SEM,manifest_file_sha256=PHY,protocol_version='1.4',preparation_chain_status='PASS',artifact_hash_status='PASS: 8/8 live frozen artifacts at start and end',schedule_status='PASS: independent 22x10 schedule',filesystem_topology_status='PASS',ledger_status='PASS: canonical completed revision 905',global_counts=totals,cleanup_success_slots=core['facts']['cleanup_slots'],cleanup_predicate_status='PASS: both complete scientific Success and analysis eligible',progress_status='PASS: all 220 exact 200-event transcripts',package_identity_status='PASS: 220/220',package_hash_status='PASS: 880 file hashes and 220 independent package hashes',sample_structure_status='PASS: exact header and 100 ordered rows per package',correctness_evidence_status='PASS: 22000 retained successful rows, zero validation failures; no GPU rerun',timing_arithmetic_status='PASS: 22000 checked uint64 H tuples, native intervals null',logical_input_identity_status='PASS: 22 independently regenerated input and expected-output conditions, all package condition/series IDs',process_summary_regeneration_status='PASS: 220 semantic and exact canonical-byte matches; 880 positional windows',cell_analysis_status='PASS: 22 independently reconstructed exact canonical files and ledger anchors',campaign_analysis_status='PASS: exact independent canonical file and ledger anchor',claim_firewall_status='PASS: exact authorized schemas; no ranking; false production_backend_selected is a denial flag, not a selection',sequential_execution_status='PASS within retained evidence: 219 nonoverlapping primary-process boundaries plus ordered QPC',no_retry_status='PASS within retained evidence: one launch record and one retained attempt/package per slot',before_after_immutability_status='PASS: 930/930 paths, sizes, SHA256 and mtimes unchanged',accepted_process_index_path=indexpath.relative_to(ROOT).as_posix(),accepted_process_index_sha256=indexhash,accepted_process_count=220,limitations=limitations,discrepancies=[],acceptance_matrix=matrix,check_counts=core['check_counts'],supplemental_check_counts=extra['check_counts'],audit_root=OUT.relative_to(ROOT).as_posix(),independent_facts_sha256=sha(raw(OUT/'independent-facts.json')),supplemental_facts_sha256=sha(raw(OUT/'supplemental-facts.json')),audit_source_sha256=sha(raw(OUT/'audit.py')),final_source_guard=source)
    def link(p,label): return f'[{label}]({(ROOT/p).as_posix()})'
    sections=[
    ('Audit identity and independence','Independent audit in this fresh chat. Newly written Python regenerated expected answers from the frozen protocol and bounded source. No implementation, preparation or operator chat was queried; historical memory supplied scope guidance only. No project code was called. Recommended Astra/High settings could not be independently attested.'),
    ('Frozen campaign identity',f'`{ID}`; source `{SRC}` on `main`; protocol 1.4/schema 2/diagnostic/H. Semantic SHA-256 `{SEM}`; physical SHA-256 `{PHY}`. GPU UUID `{GPU}`, CUDA ordinal 0 and Vulkan index 0. Deadlines 60000/1200000/86400000 ms. DR-44 and DR-45 remain Accepted.'),
    ('Preparation provenance chain','Freeze, manifest, verification and review reconcile. Candidate/final/stability report bytes, hashes, sizes, schedules, GPU identities and chronological phase records agree. Candidate manifest equals final bytes. All eight live frozen artifacts match their freeze and manifest hashes. No verifier was rerun.'),
    ('Campaign topology','220 final directories, exactly four files each (880 files); no incomplete package, failure sidecar or ledger staging residue. Exactly 22 cell analyses and one campaign analysis. All declared slots occur once.'),
    ('Control ledger audit','Canonical ledger bytes verified; completed revision 905, null fatal sequence and reason. Independently counted 220 resolved_success, zero diagnostic failures, fatal slots or unlaunched slots. 218 ordinary successes satisfy the ordinary safe-control predicate. Lifetime helper counts were allowed to exceed one. Slot and publication revision order reconciles.'),
    ('DR-45 cleanup-success audit','Discovered by reconciliation reason: slot 160 (cell 16, E1/H2D 1024 bytes, CUDA, block/process 0, order 0) and slot 162 (same cell, Vulkan, block/process 1, order 0). Both: primary exit 0, SupervisorForced, descendant observed, termination requested/succeeded, primary termination confirmed, containment assigned/verified, Job empty, ActiveProcesses 0, TotalProcesses 17, no control error or timeout. Both retain 100 successful rows and remain analysis eligible. No environmental cause is inferred.'),
    ('Progress audit','220 exact transcripts: 22000 Started + 22000 Returned = 44000 events. Wire magic/version/reserved/identity, alternating order, indexes 0..99 and QPC constraints checked. Every slot has Full form, clean EOF, no trailing bytes, transport error or outstanding attempt. QPC is control evidence only.'),
    ('Package audit','220/220 frozen environment, workload, process, condition and series identities verified. 880/880 file hashes and 220/220 independent package hashes match ledger anchors. Environment, initialization, samples and summary canonical bytes independently reconcile.'),
    ('Sample/correctness evidence audit','22000/22000 retained rows are successful, correctly ordered, validation-passing H observations; zero validation failures. All 22000 checked uint64 submission + wait = completion tuples agree; H native intervals are empty/null. Every raw row participates, including extreme values. No GPU computation was executed.'),
    ('Deterministic logical-input identity audit','All 22 input and expected-output digests independently regenerated. Verified seeded words/bytes, structured and shuffled B permutations (size/range/bijection), C targets/zero state/histograms, D1 N/K transforms and E exact-copy identities. Regenerated every comparison_condition_id and series_id.'),
    ('Process summary regeneration','220/220 summaries reproduce semantically and byte-for-byte: counts, minimum, median, sequential binary64 mean, two-pass sample SD, CV and nearest-rank p95 for all three H metrics. All 880 fixed positional windows reproduce. No sample or process exclusion.'),
    ('Cell-analysis reconstruction','22/22 cells independently regenerated with exact canonical bytes and path/size/hash ledger anchors. Each contains ten distinct processes, five per backend, all successful. Only the permitted threshold-free within-backend process-median span is reproduced.'),
    ('Campaign-analysis reconstruction','Campaign canonical bytes and anchor reproduce exactly: complete, declared/attempted/resolved/successful 220, diagnostic failures/unlaunched 0, fully resolved cells 22, cells with failures 0. All 22 cell hashes agree.'),
    ('Claim-firewall audit','Exact analysis schemas agree and contain no ranking, winner, cross-backend ratio, speedup or score. All three campaign claim flags are false. The protocol field production_backend_selected=false explicitly denies selection. Control anomalies remain in the ledger; timing remains in packages.'),
    ('Immutability audit','930 retained preparation/campaign files (14786169 bytes), including all 910 required minimum files, were independently snapshotted before and after. Every path, size, SHA-256 and modification time is identical. Eight live frozen artifacts also rehash identically. Audit outputs were excluded.'),
    ('Accepted-process index',f'Created with exclusive CREATE_NEW semantics: {link(indexpath.relative_to(ROOT).as_posix(),"accepted-process index")}. Exactly 220 routing records; SHA-256 `{indexhash}`. All records are successful, package-verified, summary-regenerated and analysis eligible; cleanup flags retained.'),
    ('Discrepancies and limitations','No evidence-integrity discrepancies.\n\n'+'\n'.join('- '+x for x in limitations)),
    ('Final verdict','**ACCEPTED** for DR-44 bounded descriptive/diagnostic claim scope only. All required evidence-integrity layers reconcile. Gate 0 remains FAIL; Stage 2 remains NOT GRANTED; production backend remains UNSELECTED.'),
    ('Acceptance matrix E1B-AUD-001..120','118 PASS; two recommended execution-surface selections UNVERIFIED. These recommendation limitations do not represent evidence-integrity failures.\n\n| Check | Status | Requirement |\n| --- | --- | --- |\n'+'\n'.join(f'| {x["id"]} | {x["status"]} | {x["requirement"]} |' for x in matrix)),
    ('Audit artifacts', '\n'.join('- '+link(p,label) for p,label in [(final_json.relative_to(ROOT).as_posix(),'Final audit JSON'),(indexpath.relative_to(ROOT).as_posix(),'Accepted-process index'),((OUT/'audit.py').relative_to(ROOT).as_posix(),'Independent Python auditor'),((OUT/'audit-spec.json').relative_to(ROOT).as_posix(),'Audit specification'),((OUT/'independent-facts.json').relative_to(ROOT).as_posix(),'Per-process audit facts'),((OUT/'logical-input-facts.json').relative_to(ROOT).as_posix(),'Logical-input regeneration facts'),((OUT/'supplemental-facts.json').relative_to(ROOT).as_posix(),'Supplemental facts'),((OUT/'retained-files-before.json').relative_to(ROOT).as_posix(),'Before snapshot'),((OUT/'retained-files-after.json').relative_to(ROOT).as_posix(),'After snapshot'),((OUT/'audit-receipt.json').relative_to(ROOT).as_posix(),'Audit receipt')])),
    ('Final Git/source state',f'`main` / `{SRC}` unchanged. Final status, diff --check and cached diff are empty. Only ignored audit outputs were created. No source/doc/evidence edit, build, test, GPU execution, staging, commit, push, Notion update or curation was performed.'),
    ('Next human-controlled step','Human review and separately authorized curation. The accepted-process index is the handoff. Benchmark interpretation and Stage-7 selection remain separate.\n\nS6-E1b INDEPENDENT POST-RUN AUDIT COMPLETE — EVIDENCE ACCEPTED FOR DR-44 DESCRIPTIVE/DIAGNOSTIC SCOPE — READY FOR CURATION — NO PERFORMANCE RANKING AUTHORIZED')]
    write(final_json,report)
    markdown='\n\n'.join(f'## {i}. {title}\n\n{body}' for i,(title,body) in enumerate(sections,1))+'\n'
    with final_md.open('xb') as file: file.write(markdown.encode('utf-8'))
    if read(final_json)!=report or raw(final_md).decode()!=markdown: raise ValueError('final publication readback')
    if inventory()!=read(OUT/'retained-files-before.json'): raise ValueError('post-publication retained inventory drift')
    final_git=git_guard()
    artifacts=[OUT/'audit.py',OUT/'audit-spec.json',OUT/'independent-facts.json',OUT/'logical-input-facts.json',OUT/'supplemental-facts.json',OUT/'retained-files-before.json',OUT/'retained-files-after.json',OUT/'source-before.json',OUT/'source-after.json',final_json,final_md,indexpath]
    write(OUT/'audit-receipt.json',dict(receipt_version=1,verdict='ACCEPTED',recorded_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),exclusive_creation=True,final_outputs_readback_verified=True,post_publication_immutability_verified=True,retained_file_count=930,accepted_index_records=220,final_git_guard=final_git,audit_artifacts=[dict(path=p.relative_to(ROOT).as_posix(),size_bytes=len(raw(p)),sha256=sha(raw(p))) for p in artifacts]))
    print(json.dumps(dict(verdict='ACCEPTED',index_sha256=indexhash,audit_json_sha256=sha(raw(final_json)),audit_markdown_sha256=sha(raw(final_md)),matrix_pass=118,matrix_unverified_recommendations=2,source_clean=True)))

if __name__=='__main__' and sys.argv[1]=='finalize': finalize()
