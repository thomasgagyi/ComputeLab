"""Independent Python stdlib verification of PowerShell-generated E2 bytes.
One invocation: validate candidate, exclusive byte publication, final recheck.
No generator import, GPU workload, production process, task registration or E1 edit.
"""
import ctypes
from datetime import datetime,timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT=Path('C:/Users/rolan/src/ComputeLab-Stage7-3060Ti');ID='ex2-s7-3060ti-e2-8f1565f'
PREP=ROOT/f'results/local/{ID}-preparation-20261009';CANDIDATE=ROOT/f'results/tmp/{ID}-prep/candidate-manifest.json';FINAL=ROOT/f'results/local/{ID}-stage6-manifest.json'
SOURCE='8f1565f7fb92e2184f4d2382aabf8fa7d00e1274';GPU='99a2369e-ca50-aac6-5c2c-fcaa44625084'
rows=[('child','out/build/x64-release/src/app/ComputeLabEx2Stage6.exe',766464,'4104e5f7237b03d5ac3265f450358e76533b099e4fdc049e62cd9ff20e9ae93c'),('supervisor','out/build/x64-release/src/app/ComputeLabEx2Stage6Supervisor.exe',659456,'33a0bebcba66e02e2f39ee4b311844c748e1a9b0c828a9c49b5e57ea989de5c8'),('a1','out/build/x64-release/src/vulkan/Ex2A1.comp.spv',1552,'c40c5ef608254d7763aa244d1ca6403605f750212a038ccf1913cff2bd61c63a'),('a2','out/build/x64-release/src/vulkan/Ex2A2.comp.spv',2188,'bfda73b6abe562facaba4fa265b281144e2c8761517ffad71c8176184cf17f3f'),('b1','out/build/x64-release/src/vulkan/Ex2B1.comp.spv',1880,'7eb4a7d40c2ea5aa133821290318b18b45c49f5284cb6ffc8ebda5f9b35e933c'),('b2','out/build/x64-release/src/vulkan/Ex2B2.comp.spv',1880,'2761fe5fab23630ce7ed47a93afe74cd769d1b154fa13a33a49601ed02120a26'),('c','out/build/x64-release/src/vulkan/Ex2C.comp.spv',1436,'7e4643308689eb49ee19a205ad126cf616e57f2c613b2e11007ad5a95446a4aa'),('d1','out/build/x64-release/src/vulkan/Ex2D1.comp.spv',1912,'3d0787c7ce83802caca54cd9bc107d506949a509ba0ffe5f0604bf72ed9e7d61')]
report={'started_utc':datetime.now(timezone.utc).isoformat(),'implementation':'Independent Python stdlib vs PowerShell/.NET generator; candidate and final checked in one bounded invocation','checks':{},'production_launches':0,'e2_task_registered':False}
def check(name,condition):
 report['checks'][name]=bool(condition)
 if not condition:raise ValueError(name)
def safe(path,required=False):
 check('inside_root:'+str(path),path.is_relative_to(ROOT))
 for p in [path,*path.parents]:
  if os.path.lexists(p):check('plain:'+str(p),not getattr(p.lstat(),'st_file_attributes',0)&0x400)
 if required:check('ordinary:'+str(path),path.is_file())
def unique(pairs):
 d={}
 for k,v in pairs:
  if k in d:raise ValueError('Duplicate JSON key '+k)
  d[k]=v
 return d
def parse(raw):return json.loads(raw.decode('utf-8','strict'),object_pairs_hook=unique,parse_constant=lambda token:(_ for _ in ()).throw(ValueError(token)))
def canon(v):return json.dumps(v,ensure_ascii=False,sort_keys=True,separators=(',',':'),allow_nan=False).encode('utf-8')
def exact(a,b):
 if type(a)!=type(b):return False
 if isinstance(a,dict):return a.keys()==b.keys() and all(exact(a[k],v) for k,v in b.items())
 if isinstance(a,list):return len(a)==len(b) and all(exact(x,y) for x,y in zip(a,b))
 return a==b
def git(*args):return subprocess.run(['git',*args],cwd=ROOT,env={**os.environ,'GIT_OPTIONAL_LOCKS':'0'},check=True,capture_output=True,text=True).stdout.strip()
def file_record(p):safe(p,True);b=p.read_bytes();return {'path':str(p),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
def new(p,b):
 with p.open('xb') as f:f.write(b);f.flush();os.fsync(f.fileno())
def run():
 check('root',Path.cwd()==ROOT);safe(ROOT)
 check('git_source_detached_clean',git('rev-parse','HEAD')==SOURCE and not git('branch','--show-current') and not git('status','--porcelain=v1','--untracked-files=all'))
 check('common_dir',git('rev-parse','--git-common-dir').replace('\\','/')=='C:/Users/rolan/Desktop/CmptLab/ComputeLab/.git')
 check('id_constraints',len(ID)<=87 and re.fullmatch('[A-Za-z0-9_-]+',ID) is not None)
 artifact_map={};actual={}
 freeze=parse((PREP/'e2-artifact-freeze.json').read_bytes())
 for key,relative,size,digest in rows:
  p=ROOT/relative;r=file_record(p);check('artifact:'+key,r['bytes']==size and r['sha256']==digest)
  artifact_map[key]={'path':relative,'size_bytes':size,'sha256':digest};actual[key]=r
  check('freeze:'+key,exact(freeze['artifacts'][key],artifact_map[key]))
  if key in ['child','supervisor']:check('embedded_root:'+key,ROOT.as_posix().encode() in p.read_bytes())
 check('eight_only',set(freeze['artifacts'])==set(actual))
 source=(ROOT/'src/ex2/Ex2Configuration.cpp').read_text();core=source.split('const std::vector<WorkloadConfiguration>& ApprovedCoreCells()',1)[1].split('return cells;',1)[0]
 conditions=[]
 for kind,body in re.findall(r'MakeConfiguration\((\w+Configuration)\{([^}]+)\}\)',core):
  parts=[p.strip() for p in body.split(',')];numeric=lambda s:int(s.replace("'",'').rstrip('U'))
  if kind=='ContentionConfiguration':conditions.append(['C',numeric(parts[0]),numeric(parts[1])])
  else:conditions.append([parts[0].split('::')[-1],numeric(parts[1])]+([numeric(parts[2]) if kind=='IterativeConfiguration' else parts[2].split('::')[-1]] if len(parts)>2 else []))
 expected_cells=[['A1',256],['A1',262144],['A1',16777216],['A2',262144],['A2',16777216],['B1',262144,'StructuredV1'],['B1',262144,'ShuffledV1'],['B1',16777216,'ShuffledV1'],['B2',262144,'StructuredV1'],['B2',262144,'ShuffledV1'],['B2',16777216,'ShuffledV1'],['C',1048576,1048576],['C',1048576,32768],['C',1048576,64],['D1',262144,16],['D1',1048576,64],['E1',1024,'HostToDevice'],['E1',1048576,'HostToDevice'],['E1',67108864,'HostToDevice'],['E2',1024,'DeviceToHost'],['E2',1048576,'DeviceToHost'],['E2',67108864,'DeviceToHost']]
 check('exact_source_cells',conditions==expected_cells)
 header=(ROOT/'src/ex2/Ex2Stage6Plan.hpp').read_text();plan=(ROOT/'src/ex2/Ex2Stage6Plan.cpp').read_text()
 check('source_order_and_samples','(block + order) % 2 == 0 ? Backend::Cuda : Backend::Vulkan' in plan and 'WarmupCount = 0, PlannedSampleCount = 100' in header)
 groups=[];schedule=[]
 for g in range(22):
  children=[]
  for block,order in enumerate([['cuda','vulkan'],['vulkan','cuda'],['cuda','vulkan'],['vulkan','cuda'],['cuda','vulkan']]):
   for position,backend in enumerate(order):
    n=g*10+block*2+position;child={'sequence_index':n,'session_id':f'{ID}-slot-{n:03d}'};children.append(child);schedule.append({**child,'cell_index':g,'block_index':block,'order_slot':position,'process_index':block,'backend':backend,'planned_samples':100,'warmup':0})
  groups.append({'cell_index':g,'declared_child_count':10,'children':children})
 safe(CANDIDATE,True);raw=CANDIDATE.read_bytes();m=parse(raw);semantic=hashlib.sha256(canon({k:v for k,v in m.items() if k!='manifest_sha256'})).hexdigest();physical=hashlib.sha256(raw).hexdigest()
 expected={'manifest_version':1,'manifest_type':'ex2-stage6-diagnostic','manifest_id':ID,'manifest_sha256':semantic,'protocol_version':'1.4','evidence_schema_version':2,'evidence_kind':'diagnostic','instrument_mode':'H','machine_id':'ex2-s7-3060ti-machine','child_executable_path':artifact_map['child']['path'],'expected_source_revision':SOURCE,'expected_git_dirty':False,'expected_child_executable_sha256':artifact_map['child']['sha256'],'expected_supervisor_executable_sha256':artifact_map['supervisor']['sha256'],'expected_gpu_uuid':GPU,'cuda_device_ordinal':0,'vulkan_physical_device_index':0,'expected_vulkan_shader_sha256':{k:artifact_map[k]['sha256'] for k in ['a1','a2','b1','b2','c','d1']},'operation_timeout_ms':60000,'child_timeout_ms':1200000,'campaign_timeout_ms':86400000,'continuation_policy':'resolved-only-no-retry','declared_cell_group_count':22,'declared_child_count':220,'groups':groups}
 check('strict_schema_types_constants_schedule',exact(m,expected))
 check('canonical_raw_bytes',raw==canon(m)+b'\n' and raw.count(b'\n')==1 and b'\r' not in raw and not raw.startswith(b'\xef\xbb\xbf'))
 paths=[f"results/local/{slot['session_id']}{s}" for slot in schedule for s in ['','.incomplete','.failure.json']]+[f'results/local/{ID}-stage6-control.json{s}' for s in ['','.incomplete','.incomplete.tmp']]+[f'results/local/{ID}-stage6-analysis',f'results/tmp/{ID}-execution']
 check('665_unique_paths',len(paths)==len(set(p.lower() for p in paths))==665)
 for relative in paths:safe(ROOT/relative);check('absent:'+relative,not os.path.lexists(ROOT/relative))
 safe(FINAL);check('final_initially_absent',not os.path.lexists(FINAL))
 probe=parse((PREP/'probe-result.json').read_bytes());check('fresh_api_obs',probe['identity_pass'] is True and probe['obs_absent'] is True and probe['cuda'][0]['uuid']==probe['vulkan'][0]['uuid']==GPU and not probe['unexpected_layer_or_injection_modules'])
 host=parse((PREP/'e2-host-preflight.json').read_bytes());check('host_no_conflict_or_restart',not host['pending_cbs'] and not host['pending_wu'] and not any(p['Name'] in ['ComputeLabEx2Stage6.exe','ComputeLabEx2Stage6Supervisor.exe'] for p in host['processes']))
 class Power(ctypes.Structure):_fields_=[('ac',ctypes.c_ubyte),('battery',ctypes.c_ubyte),('pct',ctypes.c_ubyte),('flag',ctypes.c_ubyte),('life',ctypes.c_uint32),('full',ctypes.c_uint32)]
 p=Power();k=ctypes.WinDLL('kernel32.dll');k.GetSystemPowerStatus.argtypes=[ctypes.POINTER(Power)];check('ac_online',bool(k.GetSystemPowerStatus(ctypes.byref(p))) and p.ac==1)
 check('ac_never',parse((PREP/'e2-power-setting.json').read_bytes())['passed'] is True)
 cache=(ROOT/'out/build/x64-release/CMakeCache.txt').read_text();check('release_source_sdk','CMAKE_BUILD_TYPE:STRING=Release' in cache and 'CMAKE_HOME_DIRECTORY:INTERNAL=C:/Users/rolan/src/ComputeLab-Stage7-3060Ti' in cache and 'v13.4' in cache and '1.4.363.0' in cache)
 check('sm86_retained','sm_86.cubin' in (ROOT/'results/local/ex2-s7-preparation-20261009-h2/release-cuda-elf.log').read_text())
 baseline=parse((PREP/'e1-preservation-baseline.json').read_bytes())
 for r in baseline['files']:check('e1_preserved:'+r['path'],file_record(Path(r['path']))['sha256']==r['sha256'])
 # Candidate is now independently verified. Publish exactly these bytes, never regenerate.
 new(FINAL,raw)
 check('final_exact_candidate',FINAL.read_bytes()==raw)
 final_m=parse(FINAL.read_bytes());check('final_physical',hashlib.sha256(FINAL.read_bytes()).hexdigest()==physical);check('final_semantic',hashlib.sha256(canon({k:v for k,v in final_m.items() if k!='manifest_sha256'})).hexdigest()==semantic)
 for key,relative,size,digest in rows:check('final_artifact:'+key,file_record(ROOT/relative)['sha256']==digest and (ROOT/relative).stat().st_size==size)
 for relative in paths:check('final_absent:'+relative,not os.path.lexists(ROOT/relative))
 check('final_clean',git('rev-parse','HEAD')==SOURCE and not git('status','--porcelain=v1','--untracked-files=all'))
 report.update({'verification_pass':True,'source_revision':SOURCE,'manifest_id':ID,'machine_id':'ex2-s7-3060ti-machine','final_manifest':str(FINAL),'manifest_bytes':len(raw),'semantic_sha256':semantic,'physical_sha256':physical,'candidate_and_final_identical':True,'artifact_inventory':actual,'namespace_count':665,'all_namespaces_absent':True,'namespace_paths':paths,'schedule':schedule,'conditions':conditions,'planned_observations':22000,'probe_identity_and_obs_pass':True,'e1_unchanged':True,'known_e1_limitation':'Permanent INCOMPLETE; exit 4 ControlPublicationFailure; 5 child creations; authoritative revision 20; distinct prepared temporary revision 21 never adopted; not a timeout or Stage6 descendant case','awaiting_exact_new_human_authorization':True,'human_authorization_string':f'AUTHORIZE S7-3060TI-E2 {ID} {semantic} {physical}'})
try:run()
except Exception as error:report.update({'verification_pass':False,'error':str(error),'stop_no_go':True})
report['completed_utc']=datetime.now(timezone.utc).isoformat();new(PREP/'independent-prelaunch-verification.json',(json.dumps(report,sort_keys=True,indent=2)+'\n').encode())
print(json.dumps({k:report.get(k) for k in ['verification_pass','error','manifest_id','semantic_sha256','physical_sha256','human_authorization_string']},sort_keys=True))
if not report['verification_pass']:raise SystemExit(2)
