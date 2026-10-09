"""User-authorized one-shot scheduler-owned execution. Never resumes or retries."""
from datetime import datetime,timezone
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT=Path('C:/Users/rolan/src/ComputeLab-Stage7-3060Ti')
RECORD=ROOT/'results/local/ex2-s7-e2-single-execution-20261009'
OWNED=ROOT/'results/tmp/ex2-s7-3060ti-e2-8f1565f-execution'
MID='ex2-s7-3060ti-e2-8f1565f'
SOURCE='8f1565f7fb92e2184f4d2382aabf8fa7d00e1274'
SEM='99404552f6302a7273f991d02d6532ebd06b38a6e7224c17721278d6b00899aa'
PHY='d4d93217721285bbc7d55d8fc6853a3685e0d23f1f5e1b6203244c359122e1e1'
GPU='99a2369e-ca50-aac6-5c2c-fcaa44625084'
PREP=ROOT/'results/local/ex2-s7-3060ti-e2-8f1565f-preparation-20261009'
def utc():return datetime.now(timezone.utc).isoformat()
def new(path,value):
    data=value if isinstance(value,bytes) else (json.dumps(value,sort_keys=True,indent=2)+'\n').encode()
    with path.open('xb') as f:f.write(data);f.flush();os.fsync(f.fileno())
def read(path):return json.loads(path.read_bytes())
def hashfile(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def plain(path,required=False):
    for p in [path,*path.parents]:
        if os.path.lexists(p) and getattr(p.lstat(),'st_file_attributes',0)&0x400:raise RuntimeError('Reparse path '+str(p))
    if required and not path.is_file():raise RuntimeError('Missing/nonregular '+str(path))
def command(argv,env):
    result=subprocess.run(argv,cwd=ROOT,env=env,capture_output=True,text=True)
    if result.returncode:raise RuntimeError('Query failed '+repr(argv)+': '+result.stderr)
    return result.stdout.strip()
def same(a,b):
    if type(a)!=type(b):return False
    if isinstance(a,dict):return a.keys()==b.keys() and all(same(a[k],v) for k,v in b.items())
    if isinstance(a,list):return len(a)==len(b) and all(same(x,y) for x,y in zip(a,b))
    return a==b
def canonical(v):return json.dumps(v,ensure_ascii=False,sort_keys=True,separators=(',',':'),allow_nan=False).encode()
def strict(raw):
    def pairs(items):
        result={}
        for k,v in items:
            if k in result:raise RuntimeError('Duplicate key')
            result[k]=v
        return result
    return json.loads(raw.decode('utf-8','strict'),object_pairs_hook=pairs,parse_constant=lambda x:(_ for _ in ()).throw(RuntimeError(x)))
def make_env():
    context=read(RECORD/'launch-environment-base.json');env=dict(os.environ)
    for key in list(env):
        if key.startswith('VK_') or key in ['CUDA_VISIBLE_DEVICES','CUDA_LAUNCH_BLOCKING','COMPUTELAB_EX2_D1_VALIDATION','COMPUTELAB_EX2_E_VALIDATION'] or re.match(r'^(NSIGHT|NVTX|RENDERDOC|GFXRECON|OBS_|STEAM_|ENABLE_VK_|DISABLE_VK_)',key) or re.search('PROFIL|INJECT',key):env.pop(key)
    env.update(context['required_environment']);env['DISABLE_VULKAN_OBS_CAPTURE']='1';env['GIT_OPTIONAL_LOCKS']='0'
    return env
def preflight(env,owned=False):
    plain(ROOT)
    if Path.cwd()!=ROOT:raise RuntimeError('Wrong CWD')
    facts={'utc':utc(),'head':command(['git','rev-parse','HEAD'],env),'branch':command(['git','branch','--show-current'],env),'git_status':command(['git','status','--porcelain=v1','--untracked-files=all'],env),'common':command(['git','rev-parse','--git-common-dir'],env)}
    if facts['head']!=SOURCE or facts['branch'] or facts['git_status'] or facts['common'].replace('\\','/')!='C:/Users/rolan/Desktop/CmptLab/ComputeLab/.git':raise RuntimeError('Source provenance')
    freeze=read(PREP/'e2-artifact-freeze.json');human=read(RECORD/'frozen-authority.json');artifacts={}
    for key,expected in human['artifacts'].items():
        path=ROOT/expected['path'];plain(path,True)
        live={'path':expected['path'],'size_bytes':path.stat().st_size,'sha256':hashfile(path)}
        if not same(live,expected) or not same(live,freeze['artifacts'][key]):raise RuntimeError('Artifact mismatch '+key)
        artifacts[key]=live
    path=ROOT/f'results/local/{MID}-stage6-manifest.json';plain(path,True);raw=path.read_bytes();m=strict(raw)
    if raw!=canonical(m)+b'\n' or raw.count(b'\n')!=1 or b'\r' in raw or hashlib.sha256(raw).hexdigest()!=PHY:raise RuntimeError('Manifest physical/canonical')
    semantic=hashlib.sha256(canonical({k:v for k,v in m.items() if k!='manifest_sha256'})).hexdigest()
    if semantic!=SEM or m['manifest_sha256']!=SEM:raise RuntimeError('Manifest semantic')
    groups=[{'cell_index':g,'declared_child_count':10,'children':[{'sequence_index':g*10+p,'session_id':f'{MID}-slot-{g*10+p:03d}'} for p in range(10)]} for g in range(22)]
    expected={'manifest_version':1,'manifest_type':'ex2-stage6-diagnostic','manifest_id':MID,'manifest_sha256':SEM,'protocol_version':'1.4','evidence_schema_version':2,'evidence_kind':'diagnostic','instrument_mode':'H','machine_id':'ex2-s7-3060ti-machine','child_executable_path':artifacts['child']['path'],'expected_source_revision':SOURCE,'expected_git_dirty':False,'expected_child_executable_sha256':artifacts['child']['sha256'],'expected_supervisor_executable_sha256':artifacts['supervisor']['sha256'],'expected_gpu_uuid':GPU,'cuda_device_ordinal':0,'vulkan_physical_device_index':0,'expected_vulkan_shader_sha256':{k:artifacts[k]['sha256'] for k in ['a1','a2','b1','b2','c','d1']},'operation_timeout_ms':60000,'child_timeout_ms':1200000,'campaign_timeout_ms':86400000,'continuation_policy':'resolved-only-no-retry','declared_cell_group_count':22,'declared_child_count':220,'groups':groups}
    if not same(m,expected):raise RuntimeError('Manifest schema/type/plan')
    paths=[ROOT/f"results/local/{c['session_id']}{suffix}" for g in groups for c in g['children'] for suffix in ['','.incomplete','.failure.json']]
    paths += [ROOT/f'results/local/{MID}-stage6-control.json{suffix}' for suffix in ['','.incomplete','.incomplete.tmp']]
    paths += [ROOT/f'results/local/{MID}-stage6-analysis',OWNED]
    if len(paths)!=len(set(str(p).lower() for p in paths)) or len(paths)!=665:raise RuntimeError('Output namespace count')
    for p in paths:
        plain(p)
        if owned and p==OWNED:continue
        if os.path.lexists(p):raise RuntimeError('Namespace collision '+str(p))
    if os.path.lexists(OWNED/'launch-intent.json'):raise RuntimeError('Prior launch-intent')
    procs=command(['powershell','-NoProfile','-Command',"@(Get-Process | Where-Object ProcessName -In @('ComputeLabEx2Stage6','ComputeLabEx2Stage6Supervisor') | Select-Object ProcessName,Id,Path) | ConvertTo-Json -Compress"],env)
    if procs and procs!='[]':raise RuntimeError('Live production process')
    class Power(ctypes.Structure):_fields_=[('ac',ctypes.c_ubyte),('battery',ctypes.c_ubyte),('pct',ctypes.c_ubyte),('flag',ctypes.c_ubyte),('life',ctypes.c_uint32),('full',ctypes.c_uint32)]
    power=Power();kernel=ctypes.WinDLL('kernel32.dll');kernel.GetSystemPowerStatus.argtypes=[ctypes.POINTER(Power)]
    if not kernel.GetSystemPowerStatus(ctypes.byref(power)) or power.ac!=1:raise RuntimeError('AC not online')
    scheme=command(['powercfg','/getactivescheme'],env);sleep=command(['powercfg','/query','SCHEME_CURRENT','SUB_SLEEP'],env)
    if '381b4222-f694-41f0-9685-ff5bb260df2e' not in scheme or not re.search(r'STANDBYIDLE[\s\S]*?Current AC Power Setting Index: 0x00000000',sleep):raise RuntimeError('AC sleep not Never on approved scheme')
    pending=command(['powershell','-NoProfile','-Command',"[bool]((Test-Path 'HKLM:\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\RebootPending') -or (Test-Path 'HKLM:\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\RebootRequired'))"],env)
    if pending!='False':raise RuntimeError('Pending restart flag')
    if ctypes.sizeof(ctypes.c_void_p)!=8:raise RuntimeError('Requires 64-bit process')
    import shutil
    free=shutil.disk_usage(ROOT).free
    if free<10*1024**3:raise RuntimeError('Inadequate disk free')
    facts.update({'artifacts':artifacts,'semantic_sha256':semantic,'physical_sha256':PHY,'protected_absent_count':664 if owned else 665,'wrapper_ownership_exception':owned,'power_scheme':scheme,'sleep_settings':sleep,'ac_online':True,'pending_restart_flags':False,'free_disk_bytes':free,'source_plan_contract':'22 source-owned cells; 220 fixed slots; CV/VC/CV/VC/CV; 100 observations; warmup 0; no retry'})
    return facts
def probe(env):
    argv=[sys.executable,'-B',str(RECORD/'metadata-only-probe.py')]
    process=subprocess.Popen(argv,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    stdout,stderr=process.communicate();new(OWNED/'probe-stdout.txt',stdout);new(OWNED/'probe-stderr.txt',stderr)
    result=strict(stdout) if stdout else {};new(OWNED/'probe-receipt.json',{'argv':argv,'pid':process.pid,'exit_code':process.returncode,'utc':utc(),'result':result,'environment_controls':{k:env.get(k) for k in ['DISABLE_VULKAN_OBS_CAPTURE','VK_INSTANCE_LAYERS','VK_LOADER_LAYERS_ENABLE','VK_LOADER_LAYERS_ALLOW','VK_LOADER_DEBUG','COMPUTELAB_EX2_D1_VALIDATION','COMPUTELAB_EX2_E_VALIDATION']}})
    if process.returncode or not result.get('obs_absent') or not result.get('identity_pass') or result.get('unexpected_layer_or_injection_modules'):raise RuntimeError('Point-of-use metadata probe failed')
    return result
def restore_power(env):
    scheme=command(['powercfg','/getactivescheme'],env)
    if '381b4222-f694-41f0-9685-ff5bb260df2e' not in scheme:
        new(OWNED/'power-restoration.json',{'restored':False,'reason':'Active scheme changed; do not alter another scheme','scheme':scheme});return
    change=subprocess.run(['powercfg','/change','standby-timeout-ac','30'],env=env,capture_output=True,text=True)
    readback=command(['powercfg','/query','SCHEME_CURRENT','SUB_SLEEP'],env)
    new(OWNED/'power-restoration.json',{'utc':utc(),'argv':change.args,'exit_code':change.returncode,'stdout':change.stdout,'stderr':change.stderr,'readback':readback,'restored':change.returncode==0 and bool(re.search(r'STANDBYIDLE[\s\S]*?Current AC Power Setting Index: 0x00000708',readback))})
def execute_once():
    env=make_env();os.environ.clear();os.environ.update(env)
    os.chdir(ROOT)
    process=None
    try:
        authorization=read(RECORD/'operator-launch-confirmation.json')
        if not authorization.get('nvidia_recording_features_off_checked'):raise RuntimeError('Missing NVIDIA check')
        if authorization.get('manifest_id')!=MID or authorization.get('semantic')!=SEM or authorization.get('physical')!=PHY:raise RuntimeError('Wrong human tuple')
        new(OWNED/'wrapper-started.json',{'utc':utc(),'pid':os.getpid(),'parent_pid':os.getppid(),'user':os.environ.get('USERNAME'),'ownership':'Windows Task Scheduler on-demand interactive user; no triggers/restarts; outside Codex process ownership'})
        facts=preflight(env,owned=True);new(OWNED/'point-of-use-preflight.json',facts)
        probe(env)
        # Integrity and output absence again after metadata initialization, immediately before intent.
        final=preflight(env,owned=True);new(OWNED/'immediate-precreation-preflight.json',final)
        argv=[str(ROOT/'out/build/x64-release/src/app/ComputeLabEx2Stage6Supervisor.exe'),'--manifest',f'results/local/{MID}-stage6-manifest.json']
        intent={'utc':utc(),'argv':argv,'cwd':str(ROOT),'manifest_id':MID,'semantic_sha256':SEM,'physical_sha256':PHY,'single_creation_only':True,'no_retry_no_resume':True}
        new(OWNED/'launch-intent.json',intent)
        with (OWNED/'supervisor-stdout.txt').open('xb') as stdout,(OWNED/'supervisor-stderr.txt').open('xb') as stderr:
            # Exactly ONE production process creation site; never called again on failure.
            process=subprocess.Popen(argv,cwd=ROOT,env=env,stdout=stdout,stderr=stderr)
            new(OWNED/'supervisor-created.json',{'pid':process.pid,'start_utc':utc(),'argv':argv,'cwd':str(ROOT),'status':'LAUNCHED_ONCE','wrapper_pid':os.getpid()})
            exitcode=process.wait()
            stdout.flush();stderr.flush();os.fsync(stdout.fileno());os.fsync(stderr.fileno())
        new(OWNED/'supervisor-terminal.json',{'pid':process.pid,'exit_code':exitcode,'end_utc':utc(),'status':'TERMINAL','scientific_acceptance_inferred':False,'restore_avast_immediately':True})
        restore_power(env)
        new(OWNED/'wrapper-terminal.json',{'utc':utc(),'status':'TERMINAL','supervisor_exit_code':exitcode,'restore_avast_immediately':True})
    except Exception as error:
        if process is not None and process.poll() is None:
            # A post-creation receipt failure never abandons the live native supervisor.
            process.wait()
        new(OWNED/'wrapper-failure.json',{'utc':utc(),'error':str(error),'production_created_receipt_present':(OWNED/'supervisor-created.json').exists(),'launch_intent_present':(OWNED/'launch-intent.json').exists(),'no_retry':True,'restore_avast_immediately':True})
        # Restoration follows failed launch/preflight too; no campaign retry or second creation.
        if not (OWNED/'power-restoration.json').exists():restore_power(env)
        raise

if __name__=='__main__':
    mode=sys.argv[1]
    if mode=='preflight':new(RECORD/'fast-final-preflight.json',preflight(make_env(),owned=False));print('FAST_PREFLIGHT_PASS')
    elif mode=='execute-once':execute_once()
    else:raise RuntimeError('Unknown mode')
