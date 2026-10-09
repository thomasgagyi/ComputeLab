"""E2 preparation input capture and one metadata-only probe; no production launch."""
from datetime import datetime,timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT=Path.cwd();PREP=ROOT/'results/local/ex2-s7-3060ti-e2-8f1565f-preparation-20261009'
E1='ex2-s7-3060ti-e1-8f1565f';E2='ex2-s7-3060ti-e2-8f1565f'
def new(path,data):
    if not isinstance(data,bytes):data=(json.dumps(data,sort_keys=True,indent=2)+'\n').encode()
    with path.open('xb') as f:f.write(data);f.flush();os.fsync(f.fileno())
def file_id(path):return {'path':str(path),'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
def git(*args):return subprocess.run(['git',*args],env={**os.environ,'GIT_OPTIONAL_LOCKS':'0'},check=True,capture_output=True,text=True).stdout.strip()
if ROOT!=Path('C:/Users/rolan/src/ComputeLab-Stage7-3060Ti') or git('rev-parse','HEAD')!='8f1565f7fb92e2184f4d2382aabf8fa7d00e1274' or git('branch','--show-current') or git('status','--porcelain=v1','--untracked-files=all'):raise RuntimeError('Source mismatch')
e1_paths=[]
for parent in [ROOT/'results/local',ROOT/'results/tmp']:
    for entry in parent.iterdir():
        if E1 in entry.name or entry.name in ['ex2-s7-single-execution-20261009','ex2-s7-preparation-20261009-h2','ex2-s7-final-prelaunch-review-20261009','ex2-s7-environment-remediation-20261009']:
            e1_paths.extend(p for p in entry.rglob('*') if p.is_file()) if entry.is_dir() else e1_paths.append(entry)
new(PREP/'e1-preservation-baseline.json',{'utc':datetime.now(timezone.utc).isoformat(),'files':[file_id(p) for p in sorted(set(e1_paths))],'e1_permanently_incomplete':True,'authoritative_revision':20,'distinct_prepared_temporary_revision':21,'exit_code':4,'adoption_resume_restart_forbidden':True})
frozen=json.loads((ROOT/'results/local/ex2-s7-preparation-20261009-h2/artifact-freeze.json').read_bytes())
for key,expected in frozen['artifacts'].items():
    p=ROOT/expected['path'];actual=file_id(p)
    if actual['bytes']!=expected['size_bytes'] or actual['sha256']!=expected['sha256']:raise RuntimeError('Frozen artifact drift '+key)
    if key in ['child','supervisor'] and ROOT.as_posix().encode() not in p.read_bytes():raise RuntimeError('Embedded build root mismatch')
required={'child':'4104e5f7237b03d5ac3265f450358e76533b099e4fdc049e62cd9ff20e9ae93c','supervisor':'33a0bebcba66e02e2f39ee4b311844c748e1a9b0c828a9c49b5e57ea989de5c8','a1':'c40c5ef608254d7763aa244d1ca6403605f750212a038ccf1913cff2bd61c63a','a2':'bfda73b6abe562facaba4fa265b281144e2c8761517ffad71c8176184cf17f3f','b1':'7eb4a7d40c2ea5aa133821290318b18b45c49f5284cb6ffc8ebda5f9b35e933c','b2':'2761fe5fab23630ce7ed47a93afe74cd769d1b154fa13a33a49601ed02120a26','c':'7e4643308689eb49ee19a205ad126cf616e57f2c613b2e11007ad5a95446a4aa','d1':'3d0787c7ce83802caca54cd9bc107d506949a509ba0ffe5f0604bf72ed9e7d61'}
if any(frozen['artifacts'][key]['sha256']!=sha for key,sha in required.items()):raise RuntimeError('Human frozen tuple differs')
new(PREP/'observed-frozen-artifacts.json',frozen['artifacts'])
helper=ROOT/'results/local/ex2-s7-environment-remediation-20261009/metadata-only-probe.py';new(PREP/'metadata-only-probe.py',helper.read_bytes())
env=dict(os.environ);removed=[]
for key in list(env):
    if key.startswith('VK_') or key in ['CUDA_VISIBLE_DEVICES','CUDA_LAUNCH_BLOCKING','COMPUTELAB_EX2_D1_VALIDATION','COMPUTELAB_EX2_E_VALIDATION'] or re.match(r'^(NSIGHT|NVTX|RENDERDOC|GFXRECON|OBS_|STEAM_|ENABLE_VK_|DISABLE_VK_)',key) or re.search('PROFIL|INJECT',key):removed.append(key);env.pop(key)
env['DISABLE_VULKAN_OBS_CAPTURE']='1'
new(PREP/'intended-launch-environment.json',{'required_environment':{key:env.get(key) for key in ['PATH','CUDA_PATH','VULKAN_SDK','VCPKG_ROOT']},'process_only_set':{'DISABLE_VULKAN_OBS_CAPTURE':'1'},'removed_instrumentation_variable_names':removed,'debug_force_enables_absent':True,'nvidia_recording_off_attestation_source':str(ROOT/'results/local/ex2-s7-single-execution-20261009/operator-launch-confirmation.json'),'nvidia_support_dll_not_proof_of_capture':True})
argv=[sys.executable,'-B',str(PREP/'metadata-only-probe.py')];process=subprocess.Popen(argv,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE);out,err=process.communicate();new(PREP/'probe-stdout.txt',out);new(PREP/'probe-stderr.txt',err)
new(PREP/'probe-execution.json',{'utc':datetime.now(timezone.utc).isoformat(),'argv':argv,'pid':process.pid,'exit_code':process.returncode,'helper_sha256':hashlib.sha256(helper.read_bytes()).hexdigest(),'scope':'Fresh metadata-only process; same intended launch environment; no production binaries or GPU workload','environment_file':'intended-launch-environment.json'})
if process.returncode:raise RuntimeError('Metadata probe failed; no retry')
probe=json.loads(out);new(PREP/'probe-result.json',probe)
if not probe['identity_pass'] or not probe['obs_absent'] or probe['unexpected_layer_or_injection_modules']:raise RuntimeError('GPU or layer predicate failed')
new(PREP/'initial-source-and-probe.json',{'git_head':git('rev-parse','HEAD'),'branch':git('branch','--show-current'),'git_status':git('status','--porcelain=v1','--untracked-files=all'),'common':git('rev-parse','--git-common-dir'),'probe_pass':True,'production_launches':0,'e2_id':E2})
print('E2_FROZEN_INPUTS_AND_FRESH_METADATA_PROBE_PASS')
