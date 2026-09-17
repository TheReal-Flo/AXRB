import struct
import unittest
from android_vulkan_compat import redirect_library

class VulkanRedirectTests(unittest.TestCase):
    def elf(self):
        b=bytearray(64);b[:6]=b'\x7fELF\x02\x01';struct.pack_into('<H',b,18,183);return bytes(b)
    def test_exact_strings_and_idempotence(self):
        data=self.elf()+b'libvulkan.so\0libvulkan.so.extra\0libvulkan.so\0'
        result,count=redirect_library(data)
        self.assertEqual(count,2);self.assertEqual(len(result),len(data))
        self.assertIn(b'libvulkan.so.extra\0',result)
        self.assertEqual(redirect_library(result),(result,0))
    def test_wrong_architecture_is_rejected(self):
        data=bytearray(self.elf());struct.pack_into('<H',data,18,62)
        with self.assertRaises(ValueError):redirect_library(data)
        with self.assertRaises(ValueError):redirect_library(b'not ELF')

if __name__=='__main__':unittest.main()
