import struct
import unittest
from arm64_lse_fallback import instruction, rewrite, sites


def elf(words):
    data = bytearray(512)
    data[:6] = b'\x7fELF\x02\x01'
    struct.pack_into('<H', data, 18, 183)
    struct.pack_into('<QQ', data, 32, 64, 192)
    struct.pack_into('<HHHH', data, 54, 56, 2, 64, 2)
    struct.pack_into('<IIQQQQQQ', data, 64, 1, 5, 0, 0, 0, 512, 512, 4096)
    struct.pack_into('<IIQQQQQQ', data, 120, 4, 4, 400, 400, 400, 16, 16, 4)
    struct.pack_into('<IIQQQQIIQQ', data, 256, 0, 1, 6, 320, 320, 4*len(words), 0, 0, 4, 0)
    struct.pack_into('<'+'I'*len(words), data, 320, *words)
    return data


class AtomicFallbackTests(unittest.TestCase):
    def test_real_burst_instructions(self):
        source = elf([0x88e9fd1b, 0xb8fc8109, 0xb8ea0133, 0xd65f03c0])
        patched, counts = rewrite(source)
        self.assertEqual(counts, {'cas': 1, 'swp': 1, 'ldadd': 1})
        self.assertEqual(sites(patched), [])
        self.assertEqual(rewrite(patched)[0], patched)
        # Original return instruction and note contents survive rewriting.
        self.assertEqual(patched[332:336], source[332:336])
        self.assertEqual(patched[400:416], source[400:416])
        for pos in (320,324,328):
            word = struct.unpack_from('<I',patched,pos)[0]
            self.assertEqual(word >> 26, 5)
            target = pos + (word & 0x3ffffff)*4
            self.assertGreaterEqual(target, 4096)

    def test_widths_and_orderings(self):
        for size in range(4):
            for acquire in range(2):
                for release in range(2):
                    self.assertEqual(instruction(0x08a07c00 | size<<30 | acquire<<22 | release<<15), 'cas')
                    for base, name in ((0x38208000,'swp'),(0x38200000,'ldadd')):
                        self.assertEqual(instruction(base | size<<30 | acquire<<23 | release<<22), name)
        self.assertIsNone(instruction(0x08207c00))  # CASP is a distinct operation.
        self.assertIsNone(instruction(0x885ffc09))  # LDAXR.

    def test_fail_closed(self):
        with self.assertRaises(ValueError): rewrite(b'not ELF')
        source = elf([0xb8fc8109])
        struct.pack_into('<I',source,120,0)
        with self.assertRaises(ValueError): rewrite(source)
        source = elf([0xb8fc8109])
        struct.pack_into('<Q',source,104,1<<28)
        with self.assertRaises(ValueError): rewrite(source)

    def test_literal_pool_mapping(self):
        source = elf([0xb8fc8109,0xb8fc8109])
        code_header = bytes(source[256:320])
        source.extend(bytes(512))
        struct.pack_into('<Q', source, 40, 512)
        struct.pack_into('<H', source, 60, 4)
        source[576:640] = code_header
        # $x at the first word, $d at the identical literal in the next word.
        source[900:907] = b'\0$x\0$d\0'
        struct.pack_into('<IIQQQQIIQQ',source,640,0,2,0,0,832,48,3,0,8,24)
        struct.pack_into('<IIQQQQIIQQ',source,704,0,3,0,0,900,7,0,0,1,0)
        struct.pack_into('<IBBHQQ',source,832,1,0,0,1,320,0)
        struct.pack_into('<IBBHQQ',source,856,4,0,0,1,324,0)
        self.assertEqual([pos for pos,_,_ in sites(source)], [320])
        patched, counts = rewrite(source)
        self.assertEqual(counts['swp'], 1)
        self.assertEqual(patched[324:328], source[324:328])


if __name__ == '__main__': unittest.main()
