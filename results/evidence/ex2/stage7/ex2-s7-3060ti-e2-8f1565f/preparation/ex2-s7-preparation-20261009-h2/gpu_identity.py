"""Read-only API inventory: enumerate and match physical UUID, never submit workloads."""
import ctypes as c
import json
from pathlib import Path
import sys
import uuid

TARGET = '99a2369e-ca50-aac6-5c2c-fcaa44625084'

def ok(code, api):
    if code != 0:
        raise RuntimeError(f'{api}: {code}')

def capture():
    cuda = c.WinDLL('nvcuda.dll')
    ok(cuda.cuInit(0), 'cuInit')
    version, count = c.c_int(), c.c_int()
    ok(cuda.cuDriverGetVersion(c.byref(version)), 'cuDriverGetVersion')
    ok(cuda.cuDeviceGetCount(c.byref(count)), 'cuDeviceGetCount')
    cuda_devices = []
    for ordinal in range(count.value):
        dev, raw, name = c.c_int(), (c.c_ubyte * 16)(), c.create_string_buffer(256)
        major, minor, memory = c.c_int(), c.c_int(), c.c_size_t()
        ok(cuda.cuDeviceGet(c.byref(dev), ordinal), 'cuDeviceGet')
        ok(cuda.cuDeviceGetUuid(c.byref(raw), dev), 'cuDeviceGetUuid')
        ok(cuda.cuDeviceGetName(name, 256, dev), 'cuDeviceGetName')
        ok(cuda.cuDeviceComputeCapability(c.byref(major), c.byref(minor), dev), 'cuDeviceComputeCapability')
        ok(cuda.cuDeviceTotalMem_v2(c.byref(memory), dev), 'cuDeviceTotalMem_v2')
        cuda_devices.append(dict(ordinal=ordinal, uuid=str(uuid.UUID(bytes=bytes(raw))), name=name.value.decode(), compute_capability=f'{major.value}.{minor.value}', total_memory_bytes=memory.value))
    class App(c.Structure):
        _fields_ = [('sType', c.c_uint32), ('pNext', c.c_void_p), ('pApplicationName', c.c_char_p), ('applicationVersion', c.c_uint32), ('pEngineName', c.c_char_p), ('engineVersion', c.c_uint32), ('apiVersion', c.c_uint32)]
    class Info(c.Structure):
        _fields_ = [('sType', c.c_uint32), ('pNext', c.c_void_p), ('flags', c.c_uint32), ('pApplicationInfo', c.POINTER(App)), ('enabledLayerCount', c.c_uint32), ('ppEnabledLayerNames', c.c_void_p), ('enabledExtensionCount', c.c_uint32), ('ppEnabledExtensionNames', c.c_void_p)]
    class ID(c.Structure):
        _fields_ = [('sType', c.c_uint32), ('pNext', c.c_void_p), ('deviceUUID', c.c_ubyte * 16), ('driverUUID', c.c_ubyte * 16), ('deviceLUID', c.c_ubyte * 8), ('deviceNodeMask', c.c_uint32), ('deviceLUIDValid', c.c_uint32)]
    class Props(c.Structure):
        _fields_ = [('sType', c.c_uint32), ('pNext', c.c_void_p), ('properties', c.c_uint64 * 128)]
    vk = c.WinDLL('vulkan-1.dll')
    vk.vkCreateInstance.argtypes = [c.POINTER(Info), c.c_void_p, c.POINTER(c.c_void_p)]
    vk.vkEnumeratePhysicalDevices.argtypes = [c.c_void_p, c.POINTER(c.c_uint32), c.c_void_p]
    vk.vkGetPhysicalDeviceProperties2.argtypes = [c.c_void_p, c.POINTER(Props)]
    vk.vkGetPhysicalDeviceProperties2.restype = None
    vk.vkDestroyInstance.argtypes = [c.c_void_p, c.c_void_p]
    app = App(sType=0, pApplicationName=b'Stage7 identity inventory', apiVersion=(1 << 22) | (1 << 12))
    info, instance = Info(sType=1, pApplicationInfo=c.pointer(app)), c.c_void_p()
    ok(vk.vkCreateInstance(c.byref(info), None, c.byref(instance)), 'vkCreateInstance')
    vulkan_devices = []
    try:
        n = c.c_uint32()
        ok(vk.vkEnumeratePhysicalDevices(instance, c.byref(n), None), 'vkEnumeratePhysicalDevices count')
        devices = (c.c_void_p * n.value)()
        ok(vk.vkEnumeratePhysicalDevices(instance, c.byref(n), devices), 'vkEnumeratePhysicalDevices')
        for index, device in enumerate(devices):
            identity = ID(sType=1000071004)
            props = Props(sType=1000059001, pNext=c.cast(c.pointer(identity), c.c_void_p))
            vk.vkGetPhysicalDeviceProperties2(device, c.byref(props))
            data = bytes(props.properties)
            vulkan_devices.append(dict(index=index, uuid=str(uuid.UUID(bytes=bytes(identity.deviceUUID))), name=data[20:276].split(b'\0')[0].decode(), api_version=int.from_bytes(data[0:4], 'little'), driver_version_raw=int.from_bytes(data[4:8], 'little'), vendor_id=int.from_bytes(data[8:12], 'little'), device_id=int.from_bytes(data[12:16], 'little'), driver_uuid=str(uuid.UUID(bytes=bytes(identity.driverUUID))), luid=bytes(identity.deviceLUID).hex(), luid_valid=bool(identity.deviceLUIDValid)))
    finally:
        vk.vkDestroyInstance(instance, None)
    cm = [d for d in cuda_devices if d['uuid'] == TARGET]
    vm = [d for d in vulkan_devices if d['uuid'] == TARGET]
    if len(cm) != 1 or len(vm) != 1 or cm[0]['name'] != 'NVIDIA GeForce RTX 3060 Ti' or vm[0]['name'] != cm[0]['name'] or cm[0]['compute_capability'] != '8.6':
        raise RuntimeError('Physical GPU identity mismatch')
    return dict(cuda_devices=cuda_devices, vulkan_devices=vulkan_devices, cuda_driver_api_version=version.value, cuda_device_ordinal=cm[0]['ordinal'], vulkan_physical_device_index=vm[0]['index'], cuda_uuid=cm[0]['uuid'], vulkan_uuid=vm[0]['uuid'], same_physical_gpu=True, explicitly_enabled_vulkan_layers=[], reporter='CUDA Driver and Vulkan properties2 device-ID APIs; no workload')

if __name__ == '__main__':
    result = capture()
    with Path(sys.argv[1]).open('x', encoding='utf-8', newline='\n') as out:
        json.dump(result, out, sort_keys=True, indent=2)
        out.write('\n')
    print(json.dumps(result, sort_keys=True))
