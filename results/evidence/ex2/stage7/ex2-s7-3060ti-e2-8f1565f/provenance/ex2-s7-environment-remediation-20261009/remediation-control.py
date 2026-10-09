"""Bounded integrity recapture and one metadata-only subprocess; never a campaign launcher."""
import ast
import ctypes
from datetime import datetime,timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT=Path('C:/Users/rolan/src/ComputeLab-Stage7-3060Ti')
OUT=ROOT/'results/local/ex2-s7-environment-remediation-20261009'
PREVIOUS=ROOT/'results/local/ex2-s7-final-prelaunch-review-20261009'
PREP=ROOT/'results/local/ex2-s7-preparation-20261009-h2'
SOURCE='8f1565f7fb92e2184f4d2382aabf8fa7d00e1274'
MID='ex2-s7-3060ti-e1-8f1565f'
GPU='99a2369e-ca50-aac6-5c2c-fcaa44625084'
SEM='5082991f007606e8644601f0c6d076d9850730b54ad236f504aee39afa23fcb0'
PHY='175141852d8c04509bda9342d2ff9e534950a5e95c49e8c04654029e406dad91'
FROZEN=[('child','out/build/x64-release/src/app/ComputeLabEx2Stage6.exe',766464,'4104e5f7237b03d5ac3265f450358e76533b099e4fdc049e62cd9ff20e9ae93c'),('supervisor','out/build/x64-release/src/app/ComputeLabEx2Stage6Supervisor.exe',659456,'33a0bebcba66e02e2f39ee4b311844c748e1a9b0c828a9c49b5e57ea989de5c8'),('a1','out/build/x64-release/src/vulkan/Ex2A1.comp.spv',1552,'c40c5ef608254d7763aa244d1ca6403605f750212a038ccf1913cff2bd61c63a'),('a2','out/build/x64-release/src/vulkan/Ex2A2.comp.spv',2188,'bfda73b6abe562facaba4fa265b281144e2c8761517ffad71c8176184cf17f3f'),('b1','out/build/x64-release/src/vulkan/Ex2B1.comp.spv',1880,'7eb4a7d40c2ea5aa133821290318b18b45c49f5284cb6ffc8ebda5f9b35e933c'),('b2','out/build/x64-release/src/vulkan/Ex2B2.comp.spv',1880,'2761fe5fab23630ce7ed47a93afe74cd769d1b154fa13a33a49601ed02120a26'),('c','out/build/x64-release/src/vulkan/Ex2C.comp.spv',1436,'7e4643308689eb49ee19a205ad126cf616e57f2c613b2e11007ad5a95446a4aa'),('d1','out/build/x64-release/src/vulkan/Ex2D1.comp.spv',1912,'3d0787c7ce83802caca54cd9bc107d506949a509ba0ffe5f0604bf72ed9e7d61')]
def save(name,value):
    data=value if isinstance(value,bytes) else ((json.dumps(value,sort_keys=True,indent=2,ensure_ascii=False)+'\n').encode() if not isinstance(value,str) else value.encode())
    with (OUT/name).open('xb') as f:f.write(data);f.flush();os.fsync(f.fileno())
def sha(data):return hashlib.sha256(data).hexdigest()
def pairs(items):
    result={}
    for key,value in items:
        if key in result:raise ValueError('Duplicate key '+key)
        result[key]=value
    return result
def parse(data):return json.loads(data.decode('utf-8','strict'),object_pairs_hook=pairs,parse_constant=lambda value:(_ for _ in ()).throw(ValueError(value)))
def canonical(value):return json.dumps(value,sort_keys=True,separators=(',',':'),ensure_ascii=False,allow_nan=False).encode()
def exact(a,b):
    if type(a)!=type(b):return False
    if isinstance(a,dict):return a.keys()==b.keys() and all(exact(a[k],v) for k,v in b.items())
    if isinstance(a,list):return len(a)==len(b) and all(exact(x,y) for x,y in zip(a,b))
    return a==b
def git(*args):return subprocess.run(['git',*args],cwd=ROOT,env={**os.environ,'GIT_OPTIONAL_LOCKS':'0'},check=True,capture_output=True,text=True).stdout.strip()
def plain(path,required=False):
    if not path.is_absolute() or not path.is_relative_to(ROOT):raise ValueError('Path outside worktree')
    for p in [path,*path.parents]:
        if os.path.lexists(p) and getattr(p.lstat(),'st_file_attributes',0)&0x400:raise ValueError('Reparse '+str(p))
    if required and not path.is_file():raise ValueError('Missing/nonordinary '+str(path))
def integrity(phase):
    result={'phase':phase,'utc':datetime.now(timezone.utc).isoformat(),'checks':{},'artifacts':{},'reviewed_inputs':{}}
    def require(label,condition):
        result['checks'][label]=bool(condition)
        if not condition:raise ValueError(label)
    try:
        require('actual_root',Path.cwd()==ROOT and git('rev-parse','--show-toplevel').replace('\\','/')==ROOT.as_posix())
        result['git']={'head':git('rev-parse','HEAD'),'branch':git('branch','--show-current'),'status':git('status','--porcelain=v1','--untracked-files=all'),'common':git('rev-parse','--git-common-dir')}
        require('source_detached_clean',result['git']['head']==SOURCE and not result['git']['branch'] and not result['git']['status'])
        require('common_directory',result['git']['common'].replace('\\','/')=='C:/Users/rolan/Desktop/CmptLab/ComputeLab/.git')
        for path in [ROOT,ROOT/'results',ROOT/'results/local',OUT]:plain(path)
        previous=parse((PREVIOUS/'final-integrity-inventory.json').read_bytes())
        for path,identity in previous['input_inventory'].items():
            p=Path(path);plain(p,True);data=p.read_bytes()
            require('retained_input:'+path,len(data)==identity['bytes'] and sha(data)==identity['sha256'])
            result['reviewed_inputs'][path]={'bytes':len(data),'sha256':sha(data)}
        for p in [PREVIOUS/'final-prelaunch-review.md',PREVIOUS/'final-prelaunch-receipt.json',PREVIOUS/'final-integrity-inventory.json',PREP/'review.md',PREP/'deployment-contract.md',ROOT/'AGENTS.md',ROOT/'docs/charter.md',ROOT/'docs/methodology.md',ROOT/'docs/results-format.md',ROOT/'docs/ex2.md']:
            data=p.read_bytes();result['reviewed_inputs'][str(p)]={'bytes':len(data),'sha256':sha(data)}
        for key,relative,size,expected in FROZEN:
            p=ROOT/relative;plain(p,True);data=p.read_bytes();measured=sha(data)
            require('artifact:'+key,len(data)==size and measured==expected)
            result['artifacts'][key]={'path':str(p),'bytes':len(data),'sha256':measured,'expected_sha256':expected}
        manifest=ROOT/f'results/local/{MID}-stage6-manifest.json';plain(manifest,True);raw=manifest.read_bytes();m=parse(raw)
        groups=[{'cell_index':cell,'declared_child_count':10,'children':[{'sequence_index':cell*10+position,'session_id':f'{MID}-slot-{cell*10+position:03d}'} for position in range(10)]} for cell in range(22)]
        expected={'manifest_version':1,'manifest_type':'ex2-stage6-diagnostic','manifest_id':MID,'manifest_sha256':SEM,'protocol_version':'1.4','evidence_schema_version':2,'evidence_kind':'diagnostic','instrument_mode':'H','machine_id':'ex2-s7-3060ti-machine','expected_source_revision':SOURCE,'expected_git_dirty':False,'child_executable_path':FROZEN[0][1],'expected_child_executable_sha256':FROZEN[0][3],'expected_supervisor_executable_sha256':FROZEN[1][3],'expected_vulkan_shader_sha256':{k:d for k,_,_,d in FROZEN[2:]},'expected_gpu_uuid':GPU,'cuda_device_ordinal':0,'vulkan_physical_device_index':0,'operation_timeout_ms':60000,'child_timeout_ms':1200000,'campaign_timeout_ms':86400000,'continuation_policy':'resolved-only-no-retry','declared_cell_group_count':22,'declared_child_count':220,'groups':groups}
        require('manifest_strict_schema',exact(m,expected))
        require('manifest_canonical',raw==canonical(m)+b'\n' and raw.count(b'\n')==1 and b'\r' not in raw and not raw.startswith(b'\xef\xbb\xbf'))
        semantic=sha(canonical({k:v for k,v in m.items() if k!='manifest_sha256'}));require('manifest_hashes',sha(raw)==PHY and semantic==SEM)
        result['manifest']={'path':str(manifest),'bytes':len(raw),'physical_sha256':sha(raw),'semantic_sha256':semantic}
        names=[child['session_id'] for group in groups for child in group['children']]
        paths=[f'results/local/{name}{suffix}' for name in names for suffix in ['','.incomplete','.failure.json']]+[f'results/local/{MID}-stage6-control.json{suffix}' for suffix in ['','.incomplete','.incomplete.tmp']]+[f'results/local/{MID}-stage6-analysis',f'results/tmp/{MID}-execution']
        require('namespace_count_unique',len(paths)==len(set(x.lower() for x in paths))==665)
        for relative in paths:plain(ROOT/relative);require('absent:'+relative,not os.path.lexists(ROOT/relative))
        result['namespace']={'count':665,'all_absent':True,'paths':paths}
        process=subprocess.run(['powershell','-NoProfile','-Command',"@(Get-Process | Where-Object ProcessName -In @('ComputeLabEx2Stage6','ComputeLabEx2Stage6Supervisor') | Select-Object ProcessName,Id,Path) | ConvertTo-Json -Compress"],check=True,capture_output=True,text=True)
        result['production_process_snapshot']=process.stdout.strip();require('no_live_production',not process.stdout.strip() or process.stdout.strip()=='[]')
        class P(ctypes.Structure):_fields_=[('ac',ctypes.c_ubyte),('battery',ctypes.c_ubyte),('percent',ctypes.c_ubyte),('flag',ctypes.c_ubyte),('life',ctypes.c_uint32),('full',ctypes.c_uint32)]
        power=P();kernel=ctypes.WinDLL('kernel32.dll');kernel.GetSystemPowerStatus.argtypes=[ctypes.POINTER(P)]
        result['power_source']={'query_pass':bool(kernel.GetSystemPowerStatus(ctypes.byref(power))),'ac_online':power.ac==1,'ac_raw':power.ac,'battery_raw':power.battery}
        obs=Path('C:/ProgramData/obs-studio-hook/obs-vulkan64.json');obsraw=obs.read_bytes();installed=parse(obsraw)
        require('installed_obs_disable_mapping',installed['layer']['name']=='VK_LAYER_OBS_HOOK' and installed['layer']['disable_environment']=={'DISABLE_VULKAN_OBS_CAPTURE':'1'})
        result['installed_obs_manifest']={'path':str(obs),'bytes':len(obsraw),'sha256':sha(obsraw),'contents':installed}
        result['pass']=True
    except Exception as error:result['pass']=False;result['error']=str(error)
    save(f'{phase}-integrity.json',result)
    if not result['pass']:raise RuntimeError(result['error'])
    return result

def probe_once():
    before=parse((OUT/'before-integrity.json').read_bytes())
    if not before['pass']:raise RuntimeError('Integrity prerequisite failed')
    # Extract only the already audited metadata API function; never execute/import prior auditor.
    prior=PREVIOUS/'independent-audit.py';source=prior.read_text();tree=ast.parse(source)
    function=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='api_identity')
    function_source=ast.get_source_segment(source,function)
    oldline="if re.search('vulkan|vklayer|obs|steam|renderdoc|gfxreconstruct|asw|avast',buffer.value,re.I): module_paths.append(buffer.value)"
    if oldline not in function_source:raise RuntimeError('Audited module enumeration anchor missing')
    function_source=function_source.replace(oldline,'module_paths.append(buffer.value)')
    epilogue='''\nif __name__ == '__main__':
    result=api_identity()
    result['pid']=os.getpid()
    result['pointer_bits']=ctypes.sizeof(ctypes.c_void_p)*8
    result['module_capture_while_instance_alive']=True
    result['environment_controls']={name:os.environ.get(name) for name in ['DISABLE_VULKAN_OBS_CAPTURE','VK_INSTANCE_LAYERS','VK_LOADER_LAYERS_ENABLE','VK_LOADER_LAYERS_ALLOW','VK_LOADER_LAYERS_DISABLE','VK_LOADER_DEBUG','COMPUTELAB_EX2_D1_VALIDATION','COMPUTELAB_EX2_E_VALIDATION','CUDA_VISIBLE_DEVICES','CUDA_LAUNCH_BLOCKING']}
    forbidden=[p for p in result['loaded_relevant_modules'] if re.search(r'graphics-hook|vklayer|steamoverlayvulkan|renderdoc|gfxreconstruct|asw|avast',p,re.I)]
    result['unexpected_layer_or_injection_modules']=forbidden
    result['identity_pass']=result['cuda'][0]['uuid']==result['vulkan'][0]['uuid']=='99a2369e-ca50-aac6-5c2c-fcaa44625084' and result['cuda'][0]['name']==result['vulkan'][0]['name']=='NVIDIA GeForce RTX 3060 Ti' and result['cuda'][0]['compute_capability']=='8.6'
    result['obs_absent']=not any('graphics-hook' in p.lower() or 'obs-studio-hook' in p.lower() for p in result['loaded_relevant_modules'])
    result['pass']=result['pointer_bits']==64 and result['identity_pass'] and result['obs_absent'] and not forbidden
    print(json.dumps(result,sort_keys=True))
    sys.exit(0 if result['pass'] else 2)
'''
    helper='import ctypes, json, os, re, sys, uuid\n'+function_source+epilogue
    save('metadata-only-probe.py',helper)
    child_env=dict(os.environ);removed={}
    # Only probe-process changes. Preserve PATH and necessary SDK/driver roots.
    for key in list(child_env):
        if key.startswith('VK_') or key in ['CUDA_VISIBLE_DEVICES','CUDA_LAUNCH_BLOCKING','COMPUTELAB_EX2_D1_VALIDATION','COMPUTELAB_EX2_E_VALIDATION'] or re.match(r'^(NSIGHT|NVTX|RENDERDOC|GFXRECON|OBS_|STEAM_|ENABLE_VK_|DISABLE_VK_)',key) or re.search('PROFIL|INJECT',key):removed[key]=child_env.pop(key)
    child_env['DISABLE_VULKAN_OBS_CAPTURE']='1'
    argv=[sys.executable,'-B',str(OUT/'metadata-only-probe.py')]
    record={'utc':datetime.now(timezone.utc).isoformat(),'argv':argv,'cwd':str(ROOT),'helper_sha256':sha(helper.encode()),'audited_reference_sha256':sha(prior.read_bytes()),'reference_scope':'Only api_identity function extracted; prior checker not executed/imported; module collection expanded to all DLL paths','parent_obs_disable':os.environ.get('DISABLE_VULKAN_OBS_CAPTURE'),'child_set':{'DISABLE_VULKAN_OBS_CAPTURE':'1'},'removed_probe_instrumentation_variables':removed,'path_preserved':child_env.get('PATH')==os.environ.get('PATH'),'sdk_roots':{k:child_env.get(k) for k in ['CUDA_PATH','VULKAN_SDK','VCPKG_ROOT']},'inherited_variable_names':sorted(child_env),'full_environment_values_logged':False,'probe_only_loader_debug':False}
    save('probe-invocation-before.json',record)
    process=subprocess.Popen(argv,cwd=ROOT,env=child_env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    stdout,stderr=process.communicate()
    save('probe-stdout.txt',stdout);save('probe-stderr.txt',stderr)
    record.update({'child_pid':process.pid,'exit_code':process.returncode,'stdout_sha256':sha(stdout),'stderr_sha256':sha(stderr),'parent_obs_disable_after':os.environ.get('DISABLE_VULKAN_OBS_CAPTURE')})
    save('probe-execution.json',record)
    if process.returncode:raise RuntimeError('Metadata probe failed; no fallback/retry')
    result=parse(stdout);save('probe-result.json',result)
    print('PROBE_PASS='+str(result['pass'])+' PID='+str(process.pid)+' OBS_ABSENT='+str(result['obs_absent']))
    if not result['pass']:raise RuntimeError('Probe did not pass')

if __name__=='__main__':
    mode=sys.argv[1]
    if mode in ['before','final']:result=integrity(mode);print(mode.upper()+'_INTEGRITY_PASS='+str(result['pass']))
    elif mode=='probe':probe_once()
    else:raise ValueError('Invalid mode')
