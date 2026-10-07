"""Read-only CUDA Driver and Vulkan 1.1 selector/UUID reporter; no workloads."""
import ctypes as c
import json
import sys
import uuid

def check(code, api):
    if code != 0:
        raise RuntimeError(f'{api}: {code}')

def capture():
    cuda = c.WinDLL('nvcuda.dll')
    check(cuda.cuInit(0), 'cuInit')
    count = c.c_int()
    check(cuda.cuDeviceGetCount(c.byref(count)), 'cuDeviceGetCount')
    if count.value < 1:
        raise RuntimeError('No CUDA device')
    device = c.c_int()
    check(cuda.cuDeviceGet(c.byref(device), 0), 'cuDeviceGet ordinal 0')
    raw = (c.c_ubyte * 16)()
    check(cuda.cuDeviceGetUuid(c.byref(raw), device), 'cuDeviceGetUuid')
    cuda_uuid = str(uuid.UUID(bytes=bytes(raw)))

    class App(c.Structure):
        _fields_ = [('sType', c.c_uint32), ('pNext', c.c_void_p), ('pApplicationName', c.c_char_p),
                    ('applicationVersion', c.c_uint32), ('pEngineName', c.c_char_p),
                    ('engineVersion', c.c_uint32), ('apiVersion', c.c_uint32)]
    class Info(c.Structure):
        _fields_ = [('sType', c.c_uint32), ('pNext', c.c_void_p), ('flags', c.c_uint32),
                    ('pApplicationInfo', c.POINTER(App)), ('enabledLayerCount', c.c_uint32),
                    ('ppEnabledLayerNames', c.c_void_p), ('enabledExtensionCount', c.c_uint32),
                    ('ppEnabledExtensionNames', c.c_void_p)]
    class ID(c.Structure):
        _fields_ = [('sType', c.c_uint32), ('pNext', c.c_void_p), ('deviceUUID', c.c_ubyte * 16),
                    ('driverUUID', c.c_ubyte * 16), ('deviceLUID', c.c_ubyte * 8),
                    ('deviceNodeMask', c.c_uint32), ('deviceLUIDValid', c.c_uint32)]
    class Props(c.Structure):
        _fields_ = [('sType', c.c_uint32), ('pNext', c.c_void_p), ('properties', c.c_uint64 * 128)]
    vk = c.WinDLL('vulkan-1.dll')
    vk.vkCreateInstance.argtypes = [c.POINTER(Info), c.c_void_p, c.POINTER(c.c_void_p)]
    vk.vkEnumeratePhysicalDevices.argtypes = [c.c_void_p, c.POINTER(c.c_uint32), c.c_void_p]
    vk.vkGetPhysicalDeviceProperties2.argtypes = [c.c_void_p, c.POINTER(Props)]
    vk.vkGetPhysicalDeviceProperties2.restype = None
    vk.vkDestroyInstance.argtypes = [c.c_void_p, c.c_void_p]
    app = App(sType=0, pApplicationName=b'S6-E1b identity only', apiVersion=(1 << 22) | (1 << 12))
    info = Info(sType=1, pApplicationInfo=c.pointer(app))
    instance = c.c_void_p()
    check(vk.vkCreateInstance(c.byref(info), None, c.byref(instance)), 'vkCreateInstance')
    try:
        n = c.c_uint32()
        check(vk.vkEnumeratePhysicalDevices(instance, c.byref(n), None), 'vkEnumeratePhysicalDevices count')
        if n.value < 1:
            raise RuntimeError('No Vulkan device')
        devices = (c.c_void_p * n.value)()
        check(vk.vkEnumeratePhysicalDevices(instance, c.byref(n), devices), 'vkEnumeratePhysicalDevices')
        identity = ID(sType=1000071004)
        props = Props(sType=1000059001, pNext=c.cast(c.pointer(identity), c.c_void_p))
        vk.vkGetPhysicalDeviceProperties2(devices[0], c.byref(props))
        vulkan_uuid = str(uuid.UUID(bytes=bytes(identity.deviceUUID)))
    finally:
        vk.vkDestroyInstance(instance, None)
    expected = '0340eaac-dc67-f450-d558-d47c55cc4417'
    if cuda_uuid != expected or vulkan_uuid != expected:
        raise RuntimeError(f'Wrong GPU: CUDA={cuda_uuid}, Vulkan={vulkan_uuid}')
    return dict(cuda_device_ordinal=0, cuda_uuid=cuda_uuid,
                vulkan_physical_device_index=0, vulkan_uuid=vulkan_uuid,
                expected_gpu_uuid=expected, same_physical_gpu=True,
                reporter='Python ctypes CUDA Driver/Vulkan identity APIs only')

if __name__ == '__main__':
    from pathlib import Path
    with Path(sys.argv[1]).open('x', encoding='utf-8', newline='\n') as out:
        json.dump(capture(), out, sort_keys=True, indent=2)
        out.write('\n')
