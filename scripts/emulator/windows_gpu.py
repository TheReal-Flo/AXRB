"""Read physical adapter identities and dedicated VRAM using Windows DXGI 1.1."""
import ctypes as C
import json
import os
import uuid


class Guid(C.Structure):
    _fields_ = [('data1', C.c_uint32), ('data2', C.c_uint16),
                ('data3', C.c_uint16), ('data4', C.c_ubyte * 8)]


class Luid(C.Structure):
    _fields_ = [('low', C.c_uint32), ('high', C.c_int32)]


class AdapterDescription(C.Structure):
    _fields_ = [('description', C.c_wchar * 128), ('vendor', C.c_uint32),
                ('device', C.c_uint32), ('subsystem', C.c_uint32), ('revision', C.c_uint32),
                ('dedicated_video', C.c_size_t), ('dedicated_system', C.c_size_t),
                ('shared_system', C.c_size_t), ('luid', Luid), ('flags', C.c_uint32)]


def enumerate_adapters():
    if os.name != 'nt':
        raise OSError('DXGI adapter enumeration requires Windows')

    def method(pointer, index, result, *arguments):
        address = C.cast(pointer, C.POINTER(C.POINTER(C.c_void_p))).contents[index]
        return C.WINFUNCTYPE(result, C.c_void_p, *arguments)(address)

    def release(pointer):
        if pointer:
            method(pointer, 2, C.c_ulong)(pointer)

    def checked(result):
        if result < 0:
            raise OSError(f'DXGI adapter query failed: 0x{result & 0xffffffff:08x}')

    dxgi = C.WinDLL('dxgi.dll')
    create = dxgi.CreateDXGIFactory1
    create.argtypes = [C.POINTER(Guid), C.POINTER(C.c_void_p)]
    create.restype = C.c_long
    iid = Guid.from_buffer_copy(uuid.UUID('770aae78-f26f-4dba-a829-253c83d1b387').bytes_le)
    factory = C.c_void_p()
    checked(create(C.byref(iid), C.byref(factory)))
    adapters = []
    try:
        enum = method(factory, 12, C.c_long, C.c_uint, C.POINTER(C.c_void_p))
        for index in range(128):
            adapter = C.c_void_p()
            result = enum(factory, index, C.byref(adapter))
            if result & 0xffffffff == 0x887a0002:  # DXGI_ERROR_NOT_FOUND
                break
            checked(result)
            try:
                desc = AdapterDescription()
                checked(method(adapter, 10, C.c_long, C.POINTER(AdapterDescription))(adapter, C.byref(desc)))
                adapters.append({'name': desc.description, 'vendor_id': desc.vendor, 'device_id': desc.device,
                                 'dedicated_video_bytes': desc.dedicated_video,
                                 'shared_system_bytes': desc.shared_system, 'flags': desc.flags,
                                 'luid': f'{desc.luid.high & 0xffffffff:08x}{desc.luid.low:08x}'})
            finally:
                release(adapter)
        else:
            raise OSError('DXGI adapter enumeration did not terminate')
    finally:
        release(factory)
    return adapters


if __name__ == '__main__':
    print(json.dumps(enumerate_adapters()))
