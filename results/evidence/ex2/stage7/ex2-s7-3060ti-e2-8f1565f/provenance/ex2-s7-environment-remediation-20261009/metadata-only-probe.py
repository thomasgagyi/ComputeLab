import ctypes, json, os, re, sys, uuid
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
                    module_paths.append(buffer.value)
    finally:
        vulkan.vkDestroyInstance(instance,None)
    return dict(cuda=cuda,vulkan=devices_result,cuda_driver_api_version=version.value,discoverable_layers=[dict(name=layer.name.decode(),description=layer.description.decode()) for layer in layers],requested_explicit_layers=[],loaded_relevant_modules=module_paths,api_scope='Driver and instance metadata only; no CUDA context, logical VkDevice, queue, kernel or operations',independent_of_prior_helper=True)
if __name__ == '__main__':
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
