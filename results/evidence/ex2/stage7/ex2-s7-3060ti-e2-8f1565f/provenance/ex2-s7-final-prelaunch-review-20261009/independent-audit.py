"""Fresh review-only audit, independently specified from the human frozen tuple.
No imports/execution of preparation scripts, compiler, test or campaign programs.
"""
import ctypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import uuid

ROOT = Path('C:/Users/rolan/src/ComputeLab-Stage7-3060Ti')
OUT = ROOT / 'results/local/ex2-s7-final-prelaunch-review-20261009'
OLD = ROOT / 'results/local/ex2-s7-preparation-20261009-h2'
MID = 'ex2-s7-3060ti-e1-8f1565f'
SOURCE = '8f1565f7fb92e2184f4d2382aabf8fa7d00e1274'
UUID = '99a2369e-ca50-aac6-5c2c-fcaa44625084'
SEMANTIC = '5082991f007606e8644601f0c6d076d9850730b54ad236f504aee39afa23fcb0'
PHYSICAL = '175141852d8c04509bda9342d2ff9e534950a5e95c49e8c04654029e406dad91'
FROZEN = [
 ('child','out/build/x64-release/src/app/ComputeLabEx2Stage6.exe',766464,'4104e5f7237b03d5ac3265f450358e76533b099e4fdc049e62cd9ff20e9ae93c'),
 ('supervisor','out/build/x64-release/src/app/ComputeLabEx2Stage6Supervisor.exe',659456,'33a0bebcba66e02e2f39ee4b311844c748e1a9b0c828a9c49b5e57ea989de5c8'),
 ('a1','out/build/x64-release/src/vulkan/Ex2A1.comp.spv',1552,'c40c5ef608254d7763aa244d1ca6403605f750212a038ccf1913cff2bd61c63a'),
 ('a2','out/build/x64-release/src/vulkan/Ex2A2.comp.spv',2188,'bfda73b6abe562facaba4fa265b281144e2c8761517ffad71c8176184cf17f3f'),
 ('b1','out/build/x64-release/src/vulkan/Ex2B1.comp.spv',1880,'7eb4a7d40c2ea5aa133821290318b18b45c49f5284cb6ffc8ebda5f9b35e933c'),
 ('b2','out/build/x64-release/src/vulkan/Ex2B2.comp.spv',1880,'2761fe5fab23630ce7ed47a93afe74cd769d1b154fa13a33a49601ed02120a26'),
 ('c','out/build/x64-release/src/vulkan/Ex2C.comp.spv',1436,'7e4643308689eb49ee19a205ad126cf616e57f2c613b2e11007ad5a95446a4aa'),
 ('d1','out/build/x64-release/src/vulkan/Ex2D1.comp.spv',1912,'3d0787c7ce83802caca54cd9bc107d506949a509ba0ffe5f0604bf72ed9e7d61')]
MINIMUM = 'review.md deployment-contract.md artifact-freeze.json final-verification.json stability-verification.json candidate-verification.json gpu-initial.json gpu-freeze.json debug-build-exit.txt debug-configure.yaml debug-ninja.log debug-smoke-exit.txt debug-smoke.log release-build-exit.txt release-build.log release-cache.txt release-ninja.log release-cuda-elf.log direct-smoke-exit.txt direct-smoke.json direct-smoke.log supervisor-smoke-exit.txt supervisor-smoke.json supervisor-smoke.log preparation-index.json publication.json'.split()
observations = {'started_utc':datetime.now(timezone.utc).isoformat(),'invocation':sys.argv,'review_implementation':'New independent-audit.py; human constants; no preparation code imports','input_inventory':{},'checks':{},'hard_blockers':[]}

def gate(name, condition):
    observations['checks'][name] = bool(condition)
    if not condition:
        raise ValueError(name)

def byte_read(path):
    path = Path(path)
    gate('path_inside_root:' + str(path), path.is_absolute() and path.is_relative_to(ROOT))
    for part in [path,*path.parents]:
        if os.path.lexists(part):
            gate('plain_path:' + str(part), not getattr(part.lstat(),'st_file_attributes',0) & 0x400)
    gate('ordinary_file:' + str(path), path.is_file())
    data = path.read_bytes()
    observations['input_inventory'][str(path)] = dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
    return data

def unique_object(pairs):
    names = [key for key,_ in pairs]
    if len(names) != len(set(names)):
        raise ValueError('duplicate JSON keys')
    return dict(pairs)

def strict_json(data):
    return json.loads(data.decode('utf-8','strict'),object_pairs_hook=unique_object,parse_constant=lambda token: (_ for _ in ()).throw(ValueError('nonfinite ' + token)))

def exact_json(actual, expected):
    if type(actual) != type(expected):
        return False
    if isinstance(actual,dict):
        return actual.keys()==expected.keys() and all(exact_json(actual[key],value) for key,value in expected.items())
    if isinstance(actual,list):
        return len(actual)==len(expected) and all(exact_json(a,b) for a,b in zip(actual,expected))
    return actual==expected

def git(*arguments):
    completed = subprocess.run(['git',*arguments],cwd=ROOT,env={**os.environ,'GIT_OPTIONAL_LOCKS':'0'},capture_output=True,text=True,check=True)
    return completed.stdout.strip()

def save(name, value):
    encoded = (json.dumps(value,sort_keys=True,indent=2,ensure_ascii=False) + '\n').encode('utf-8')
    with (OUT/name).open('xb') as stream:
        stream.write(encoded); stream.flush(); os.fsync(stream.fileno())

def api_identity():
    # Explicit ctypes prototypes, UUID v2 and separate CUDA attribute queries.
    c = ctypes
    driver = c.WinDLL('nvcuda.dll')
    def cu(name,types,*values):
        function=getattr(driver,name); function.argtypes=types; function.restype=c.c_int
        code=function(*values)
        if code: raise RuntimeError(f'{name} returned {code}')
    cu('cuInit',[c.c_uint],0)
    count=c.c_int(); version=c.c_int()
    cu('cuDeviceGetCount',[c.POINTER(c.c_int)],c.byref(count))
    cu('cuDriverGetVersion',[c.POINTER(c.c_int)],c.byref(version))
    cuda=[]
    for ordinal in range(count.value):
        dev=c.c_int(); raw=(c.c_ubyte*16)(); name=c.create_string_buffer(256); major=c.c_int(); minor=c.c_int()
        cu('cuDeviceGet',[c.POINTER(c.c_int),c.c_int],c.byref(dev),ordinal)
        cu('cuDeviceGetUuid_v2',[c.c_void_p,c.c_int],c.byref(raw),dev.value)
        cu('cuDeviceGetName',[c.c_void_p,c.c_int,c.c_int],name,256,dev.value)
        cu('cuDeviceGetAttribute',[c.POINTER(c.c_int),c.c_int,c.c_int],c.byref(major),75,dev.value)
        cu('cuDeviceGetAttribute',[c.POINTER(c.c_int),c.c_int,c.c_int],c.byref(minor),76,dev.value)
        cuda.append(dict(ordinal=ordinal,uuid=str(uuid.UUID(bytes=bytes(raw))),name=name.value.decode(),compute_capability=f'{major.value}.{minor.value}'))
    class Application(c.Structure):
        _fields_=[('type',c.c_uint32),('next',c.c_void_p),('name',c.c_char_p),('version',c.c_uint32),('engine',c.c_char_p),('engineVersion',c.c_uint32),('apiVersion',c.c_uint32)]
    class InstanceInfo(c.Structure):
        _fields_=[('type',c.c_uint32),('next',c.c_void_p),('flags',c.c_uint32),('application',c.POINTER(Application)),('layerCount',c.c_uint32),('layers',c.c_void_p),('extensionCount',c.c_uint32),('extensions',c.c_void_p)]
    class DeviceID(c.Structure):
        _fields_=[('type',c.c_uint32),('next',c.c_void_p),('uuid',c.c_ubyte*16),('driverUUID',c.c_ubyte*16),('luid',c.c_ubyte*8),('nodeMask',c.c_uint32),('luidValid',c.c_uint32)]
    class DeviceProperties(c.Structure):
        _fields_=[('apiVersion',c.c_uint32),('driverVersion',c.c_uint32),('vendorID',c.c_uint32),('deviceID',c.c_uint32),('deviceType',c.c_uint32),('name',c.c_char*256),('pipelineUUID',c.c_ubyte*16),('remaining_properties_aligned',c.c_uint64*128)]
    class Properties2(c.Structure):
        _fields_=[('type',c.c_uint32),('next',c.c_void_p),('properties',DeviceProperties)]
    class Layer(c.Structure):
        _fields_=[('name',c.c_char*256),('specification',c.c_uint32),('implementation',c.c_uint32),('description',c.c_char*256)]
    vulkan=c.WinDLL('vulkan-1.dll')
    vulkan.vkCreateInstance.argtypes=[c.POINTER(InstanceInfo),c.c_void_p,c.POINTER(c.c_void_p)]; vulkan.vkCreateInstance.restype=c.c_int32
    vulkan.vkEnumeratePhysicalDevices.argtypes=[c.c_void_p,c.POINTER(c.c_uint32),c.c_void_p]; vulkan.vkEnumeratePhysicalDevices.restype=c.c_int32
    vulkan.vkGetPhysicalDeviceProperties2.argtypes=[c.c_void_p,c.POINTER(Properties2)]; vulkan.vkGetPhysicalDeviceProperties2.restype=None
    vulkan.vkDestroyInstance.argtypes=[c.c_void_p,c.c_void_p]; vulkan.vkDestroyInstance.restype=None
    vulkan.vkEnumerateInstanceLayerProperties.argtypes=[c.POINTER(c.c_uint32),c.c_void_p]; vulkan.vkEnumerateInstanceLayerProperties.restype=c.c_int32
    def vk_ok(result):
        if result: raise RuntimeError(f'Vulkan metadata returned {result}')
    layer_count=c.c_uint32(); vk_ok(vulkan.vkEnumerateInstanceLayerProperties(c.byref(layer_count),None))
    layers=(Layer*layer_count.value)(); vk_ok(vulkan.vkEnumerateInstanceLayerProperties(c.byref(layer_count),layers))
    app=Application(type=0,name=b'Independent Stage7 read-only review',apiVersion=(1<<22)|(3<<12))
    info=InstanceInfo(type=1,application=c.pointer(app)); instance=c.c_void_p()
    vk_ok(vulkan.vkCreateInstance(c.byref(info),None,c.byref(instance)))
    devices_result=[]
    try:
        n=c.c_uint32(); vk_ok(vulkan.vkEnumeratePhysicalDevices(instance,c.byref(n),None))
        devices=(c.c_void_p*n.value)(); vk_ok(vulkan.vkEnumeratePhysicalDevices(instance,c.byref(n),devices))
        for index,device in enumerate(devices):
            device_id=DeviceID(type=1000071004); props=Properties2(type=1000059001,next=c.cast(c.pointer(device_id),c.c_void_p))
            vulkan.vkGetPhysicalDeviceProperties2(device,c.byref(props))
            p=props.properties
            devices_result.append(dict(index=index,uuid=str(uuid.UUID(bytes=bytes(device_id.uuid))),name=p.name.decode(),driver_uuid=str(uuid.UUID(bytes=bytes(device_id.driverUUID))),api_version=p.apiVersion,driver_version_raw=p.driverVersion,vendor_id=p.vendorID,device_id=p.deviceID,luid=bytes(device_id.luid).hex(),luid_valid=bool(device_id.luidValid)))
        psapi=c.WinDLL('psapi.dll'); kernel=c.WinDLL('kernel32.dll'); kernel.GetCurrentProcess.restype=c.c_void_p
        modules=(c.c_void_p*1024)(); needed=c.c_uint32()
        psapi.EnumProcessModules.argtypes=[c.c_void_p,c.c_void_p,c.c_uint32,c.POINTER(c.c_uint32)]
        psapi.GetModuleFileNameExW.argtypes=[c.c_void_p,c.c_void_p,c.c_wchar_p,c.c_uint32]
        process=kernel.GetCurrentProcess(); module_paths=[]
        if psapi.EnumProcessModules(process,modules,c.sizeof(modules),c.byref(needed)):
            for module in modules[:needed.value//c.sizeof(c.c_void_p)]:
                buffer=c.create_unicode_buffer(32768)
                if psapi.GetModuleFileNameExW(process,module,buffer,len(buffer)):
                    if re.search('vulkan|vklayer|obs|steam|renderdoc|gfxreconstruct|asw|avast',buffer.value,re.I): module_paths.append(buffer.value)
    finally:
        vulkan.vkDestroyInstance(instance,None)
    return dict(cuda=cuda,vulkan=devices_result,cuda_driver_api_version=version.value,discoverable_layers=[dict(name=layer.name.decode(),description=layer.description.decode()) for layer in layers],requested_explicit_layers=[],loaded_relevant_modules=module_paths,api_scope='Driver and instance metadata only; no CUDA context, logical VkDevice, queue, kernel or operations',independent_of_prior_helper=True)

def run():
    gate('cwd',Path.cwd()==ROOT)
    observations['git_initial']=dict(head=git('rev-parse','HEAD'),branch=git('branch','--show-current'),status=git('status','--porcelain=v1','--untracked-files=all'),common=git('rev-parse','--git-common-dir'),top=git('rev-parse','--show-toplevel'))
    gate('source_detached_clean',observations['git_initial']['head']==SOURCE and not observations['git_initial']['branch'] and not observations['git_initial']['status'])
    gate('common_directory',observations['git_initial']['common'].replace('\\','/')=='C:/Users/rolan/Desktop/CmptLab/ComputeLab/.git')
    for name in MINIMUM: byte_read(OLD/name)
    old_index=strict_json(byte_read(OLD/'preparation-index.json'))
    bad_index=[]
    for path,identity in old_index['retained_files'].items():
        data=byte_read(Path(path))
        if len(data)!=identity['bytes'] or hashlib.sha256(data).hexdigest()!=identity['sha256']: bad_index.append(path)
    observations['preparation_index_audit']=dict(indexed_input_count=len(old_index['retained_files']),missing_or_changed=bad_index,unindexed_named_inputs=[name for name in MINIMUM if str(OLD/name) not in old_index['retained_files']],note='Prior review.md and preparation-index.json are self-publication inputs without an earlier self-digest; audited now, no fabricated prior anchor.')
    gate('retained_index_exact',not bad_index)
    frozen=strict_json(byte_read(OLD/'artifact-freeze.json'))
    ps=strict_json(byte_read(OUT/'powershell-hash-crosscheck.json')); ps_by_path={row['path']:row for row in ps}
    artifacts={}
    for name,relative,length,digest in FROZEN:
        path=ROOT/relative; data=byte_read(path); measured=hashlib.sha256(data).hexdigest()
        gate('artifact:' + name,len(data)==length and measured==digest)
        gate('freeze_record:' + name,exact_json(frozen['artifacts'][name],dict(path=relative,size_bytes=length,sha256=digest)))
        cross=ps_by_path[str(path)]; gate('hash_implementations:' + name,cross['bytes']==length and cross['sha256']==digest)
        if name in ['child','supervisor']: gate('embedded_build_root:' + name,ROOT.as_posix().encode() in data)
        artifacts[name]=dict(path=str(path),relative_path=relative,bytes=len(data),expected_bytes=length,expected_sha256=digest,python_sha256=measured,powershell_sha256=cross['sha256'],pass_check=True)
    observations['artifacts']=artifacts
    final=ROOT/f'results/local/{MID}-stage6-manifest.json'; raw=byte_read(final)
    m=strict_json(raw)
    # Independently specify parser-owned keys/types and full expected schedule.
    expected={'manifest_version':1,'manifest_type':'ex2-stage6-diagnostic','manifest_id':MID,'manifest_sha256':SEMANTIC,'protocol_version':'1.4','evidence_schema_version':2,'evidence_kind':'diagnostic','instrument_mode':'H','machine_id':'ex2-s7-3060ti-machine','expected_source_revision':SOURCE,'expected_git_dirty':False,'child_executable_path':FROZEN[0][1],'expected_child_executable_sha256':FROZEN[0][3],'expected_supervisor_executable_sha256':FROZEN[1][3],'expected_vulkan_shader_sha256':{row[0]:row[3] for row in FROZEN[2:]},'expected_gpu_uuid':UUID,'cuda_device_ordinal':0,'vulkan_physical_device_index':0,'operation_timeout_ms':60000,'child_timeout_ms':1200000,'campaign_timeout_ms':86400000,'continuation_policy':'resolved-only-no-retry','declared_cell_group_count':22,'declared_child_count':220,'groups':[]}
    cells=[('A1',256),('A1',262144),('A1',16777216),('A2',262144),('A2',16777216),('B1',262144,'StructuredV1'),('B1',262144,'ShuffledV1'),('B1',16777216,'ShuffledV1'),('B2',262144,'StructuredV1'),('B2',262144,'ShuffledV1'),('B2',16777216,'ShuffledV1'),('C',1048576,1048576),('C',1048576,32768),('C',1048576,64),('D1',262144,16),('D1',1048576,64),('E1',1024,'HostToDevice'),('E1',1048576,'HostToDevice'),('E1',67108864,'HostToDevice'),('E2',1024,'DeviceToHost'),('E2',1048576,'DeviceToHost'),('E2',67108864,'DeviceToHost')]
    source=byte_read(ROOT/'src/ex2/Ex2Configuration.cpp').decode(); core=source.split('const std::vector<WorkloadConfiguration>& ApprovedCoreCells()',1)[1].split('return cells;',1)[0]
    source_cells=[]
    for condition in re.findall(r'MakeConfiguration\((\w+Configuration)\{([^}]+)\}\)',core):
        kind,body=condition; items=[part.strip() for part in body.split(',')]
        def num(s): return int(s.replace("'",'').rstrip('U'))
        if kind=='ContentionConfiguration': source_cells.append(('C',num(items[0]),num(items[1]))); gate('counter_allocation',num(items[2])==1048576)
        else:
            variant=items[0].split('::')[-1]; row=[variant,num(items[1])]
            if len(items)==3: row.append(num(items[2]) if kind=='IterativeConfiguration' else items[2].split('::')[-1])
            source_cells.append(tuple(row))
    gate('source_22_conditions',source_cells==cells)
    plan=byte_read(ROOT/'src/ex2/Ex2Stage6Plan.cpp').decode(); header=byte_read(ROOT/'src/ex2/Ex2Stage6Plan.hpp').decode()
    gate('source_owned_order','(block + order) % 2 == 0 ? Backend::Cuda : Backend::Vulkan' in plan)
    gate('source_owned_sample_controls','WarmupCount = 0, PlannedSampleCount = 100' in header and 'CoreCellCount = 22, PairedBlockCount = 5' in header)
    schedule=[]
    for cell in range(22):
        children=[]
        for block in range(5):
            backends=['cuda','vulkan'] if block%2==0 else ['vulkan','cuda']
            for order_slot,backend in enumerate(backends):
                sequence=cell*10+block*2+order_slot; session=f'{MID}-slot-{sequence:03d}'
                children.append({'sequence_index':sequence,'session_id':session})
                schedule.append(dict(sequence_index=sequence,session_id=session,cell_index=cell,backend=backend,block=block,process_index=block,order_slot=order_slot,operations=100,warmup=0))
        expected['groups'].append({'cell_index':cell,'declared_child_count':10,'children':children})
    gate('strict_manifest_schema_types_values',exact_json(m,expected))
    canonical=json.dumps(m,ensure_ascii=False,sort_keys=True,separators=(',',':'),allow_nan=False).encode('utf-8')
    gate('canonical_manifest_bytes',raw==canonical+b'\n' and b'\r' not in raw and raw.count(b'\n')==1 and not raw.startswith(b'\xef\xbb\xbf'))
    payload={key:value for key,value in m.items() if key!='manifest_sha256'}
    semantic=hashlib.sha256(json.dumps(payload,ensure_ascii=False,sort_keys=True,separators=(',',':'),allow_nan=False).encode('utf-8')).hexdigest()
    physical=hashlib.sha256(raw).hexdigest()
    gate('two_manifest_hashes',semantic==SEMANTIC and physical==PHYSICAL and ps_by_path[str(final)]['sha256']==PHYSICAL)
    candidate=ROOT/f'results/tmp/{MID}-prep/candidate-manifest.json'; gate('candidate_final_identical',byte_read(candidate)==raw)
    for phase in ['candidate','final','stability']:
        receipt=strict_json(byte_read(OLD/f'{phase}-verification.json'))
        gate('prior_receipt:' + phase,receipt['verification_pass'] is True and receipt['phase']==phase and receipt['semantic_sha256']==SEMANTIC and receipt['physical_sha256']==PHYSICAL and receipt['source_revision']==SOURCE and receipt['git_clean'] is True and receipt['absent_paths_checked']==665)
        expected_artifacts={name:dict(path=relative,size_bytes=length,sha256=digest) for name,relative,length,digest in FROZEN}
        gate('prior_receipt_artifacts:' + phase,exact_json(receipt['artifacts'],expected_artifacts))
    pub=strict_json(byte_read(OLD/'publication.json'))
    gate('publication_receipt',pub['physical_sha256']==PHYSICAL and pub['production_supervisor_invoked'] is False and pub['candidate_receipt_sha256']==hashlib.sha256(byte_read(OLD/'candidate-verification.json')).hexdigest())
    absent=[f"results/local/{slot['session_id']}{suffix}" for slot in schedule for suffix in ['', '.incomplete', '.failure.json']]
    absent += [f'results/local/{MID}-stage6-control.json{suffix}' for suffix in ['', '.incomplete', '.incomplete.tmp']]
    absent += [f'results/local/{MID}-stage6-analysis',f'results/tmp/{MID}-execution']
    gate('namespace_unique_665',len(absent)==len(set(p.lower() for p in absent))==665)
    for relative in absent:
        path=ROOT/relative
        for part in [path,*path.parents]:
            if os.path.lexists(part): gate('namespace_plain:' + str(part),not getattr(part.lstat(),'st_file_attributes',0)&0x400)
        gate('absent:' + relative,not os.path.lexists(path))
    observations['namespace']=dict(count=665,absent_paths=absent,all_absent=True,smoke_residue=[str(p) for p in (ROOT/'results/local').glob('s6-i*-smoke-*')])
    gate('no_smoke_residue',not observations['namespace']['smoke_residue'])
    observations['manifest']=dict(path=str(final),bytes=len(raw),expected_physical=PHYSICAL,measured_physical=physical,expected_semantic=SEMANTIC,measured_semantic=semantic,candidate_identical=True,strict_schema_and_canonical=True)
    observations['plan']=dict(cells=cells,schedule=schedule,cell_count=22,slots=220,operations=22000,cuda_processes=110,vulkan_processes=110,warmup=0,deadlines_ms=[60000,1200000,86400000],continuation='resolved-only-no-retry')
    # Read all retained smoke evidence; never execute the tests.
    for name in ['debug-build','debug-smoke','release-build','direct-smoke','supervisor-smoke']: gate('prior_exit:' + name,byte_read(OLD/f'{name}-exit.txt').decode().strip()=='0')
    debug=byte_read(OLD/'debug-last-test.log').decode(); gate('three_debug_smokes',debug.count('Test Passed.')==3 and all(f'Test: ComputeLabSmoke.{name}' in debug for name in ['CUDA','Vulkan','GpuIdentity']) and UUID.replace('-','') in debug)
    for kind,suite in [('direct','Ex2Stage6ReleaseSmoke'),('supervisor','Ex2Stage6SupervisorReleaseSmoke')]:
        results=strict_json(byte_read(OLD/f'{kind}-smoke.json')); cases=results['testsuites'][0]['testsuite']
        gate('four_' + kind,results['tests']==4 and all(results[key]==0 for key in ['failures','errors','disabled']) and results['testsuites'][0]['name']==suite and [case['name'] for case in cases]==['A1Cuda','A1Vulkan','E1Cuda','E1Vulkan'] and all(case['status']=='RUN' and case['result']=='COMPLETED' and 'failures' not in case for case in cases))
        log=byte_read(OLD/f'{kind}-smoke.log').decode()
        gate('smoke_output:' + kind,log.count('successful=100')==4 if kind=='direct' else log.count('samples=100 progress=200')==4 and log.count('job_active=0')==4)
    for relative,retained in [('out/build/x64-release/.ninja_log','release-ninja.log'),('out/build/x64-release/CMakeCache.txt','release-cache.txt'),('out/build/x64-release/build.ninja','release-build.ninja')]:
        gate('build_metadata_unchanged:' + relative,byte_read(ROOT/relative)==byte_read(OLD/retained))
    gate('seven_shader_builds',all(f'Generating {name}.comp.spv' in byte_read(OLD/'release-build.log').decode() for name in ['Ex2A1','Ex2A2','Ex2B1','Ex2B2','Ex2C','Ex2D1','Ex1Transform']))
    byte_read(ROOT/'src/ex2/Ex2Stage6Supervisor.cpp'); byte_read(ROOT/'src/ex2/Ex2Stage6Progress.hpp'); byte_read(ROOT/'src/app/Ex2Stage6Execution.cpp'); byte_read(ROOT/'src/app/Ex2Stage6SupervisorMain.cpp')
    host=strict_json(byte_read(OUT/'final-host-state.json')); gate('no_live_production',not host['production_processes'])
    gate('ntfs',host['drive']['DriveFormat']=='NTFS')
    gpu=api_identity(); observations['gpu']=gpu; save('independent-api-identity.json',gpu)
    gate('independent_gpu_api_match',len(gpu['cuda'])>0 and len(gpu['vulkan'])>0 and gpu['cuda'][0]['uuid']==gpu['vulkan'][0]['uuid']==UUID and gpu['cuda'][0]['name']==gpu['vulkan'][0]['name']=='NVIDIA GeForce RTX 3060 Ti' and gpu['cuda'][0]['compute_capability']=='8.6')
    observations['git_final']=dict(head=git('rev-parse','HEAD'),branch=git('branch','--show-current'),status=git('status','--porcelain=v1','--untracked-files=all'))
    gate('final_clean_source',observations['git_final']['head']==SOURCE and not observations['git_final']['branch'] and not observations['git_final']['status'])
    for name,relative,length,digest in FROZEN: gate('artifact_stability:' + name,hashlib.sha256(byte_read(ROOT/relative)).hexdigest()==digest)
    gate('manifest_stability',byte_read(final)==raw)
    observations['immutable_checks_pass']=True

if __name__=='__main__':
    try:
        run()
    except Exception as error:
        observations['immutable_checks_pass']=False
        observations['hard_blockers'].append(str(error))
        observations['error_type']=type(error).__name__
    observations['completed_utc']=datetime.now(timezone.utc).isoformat()
    save('final-integrity-inventory.json',observations)
    print(json.dumps(dict(immutable_checks_pass=observations['immutable_checks_pass'],hard_blockers=observations['hard_blockers'],input_count=len(observations['input_inventory']),gpu=observations.get('gpu')),sort_keys=True))
    sys.exit(0 if observations['immutable_checks_pass'] else 2)
