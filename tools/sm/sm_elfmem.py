import struct, sys
class Mem:
    def __init__(self, path):
        self.b = open(path, 'rb').read()
        b = self.b
        phoff, = struct.unpack_from('<I', b, 0x1c)
        phentsize, phnum = struct.unpack_from('<HH', b, 0x2a)
        self.segs = []
        for i in range(phnum):
            t, off, va, pa, fsz, msz, fl, al = struct.unpack_from('<8I', b, phoff + i * phentsize)
            if t == 1:
                self.segs.append((va, off, fsz))
    def off(self, va):
        for v, o, s in self.segs:
            if v <= va < v + s:
                return o + va - v
        raise KeyError(hex(va))
    def u32(self, va):
        return struct.unpack_from('<I', self.b, self.off(va))[0]
    def f32(self, va):
        return struct.unpack_from('<f', self.b, self.off(va))[0]
    def s(self, va):
        if not va: return None
        try:
            o = self.off(va)
        except KeyError:
            return '<%x>' % va
        e = self.b.find(b'\0', o)
        return self.b[o:e].decode('latin1')
