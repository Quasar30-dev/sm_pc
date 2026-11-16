// High Impact Games disc/container readers — docs/HIG_FORMATS.md.
// TJZIP grammar from the game's own decompressor symbols
// (TJZIP_ParseRawDataBlock / ParseDictionaryCode / Decompress); reference
// python in tjzip_dump.py. Little-endian throughout.
#include "hig/higwad.h"

#include <cstdio>
#include <cstring>
#include <array>

namespace hig {

namespace {

constexpr uint32_t kSector = 2048;

uint32_t rd_u32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

}  // namespace

// ---- ISO9660 ------------------------------------------------------------------

bool IsoFs::open(const std::wstring& iso_path, std::string* err) {
    close();
    FILE* f = _wfopen(iso_path.c_str(), L"rb");
    if (!f) {
        if (err) *err = "cannot open ISO image";
        return false;
    }
    fp_ = f;
    uint8_t pvd[kSector];
    if (_fseeki64(f, 16ll * kSector, SEEK_SET) != 0 ||
        fread(pvd, 1, kSector, f) != kSector || pvd[0] != 1 ||
        std::memcmp(pvd + 1, "CD001", 5) != 0) {
        if (err) *err = "no ISO9660 primary volume descriptor";
        close();
        return false;
    }
    volume_id_.assign(reinterpret_cast<char*>(pvd + 40), 32);
    while (!volume_id_.empty() &&
           (volume_id_.back() == ' ' || volume_id_.back() == 0))
        volume_id_.pop_back();
    uint32_t root_lba = rd_u32(pvd + 156 + 2);
    uint32_t root_size = rd_u32(pvd + 156 + 10);
    files_.clear();
    walk(root_lba, root_size, "", 0);
    return true;
}

void IsoFs::close() {
    if (fp_) {
        fclose(static_cast<FILE*>(fp_));
        fp_ = nullptr;
    }
    files_.clear();
    volume_id_.clear();
}

void IsoFs::walk(uint32_t lba, uint32_t size, const std::string& prefix,
                 int depth) {
    if (depth > 8 || !fp_)
        return;
    FILE* f = static_cast<FILE*>(fp_);
    std::vector<uint8_t> dir(size);
    if (_fseeki64(f, int64_t(lba) * kSector, SEEK_SET) != 0 ||
        fread(dir.data(), 1, size, f) != size)
        return;
    size_t pos = 0;
    while (pos < dir.size()) {
        uint8_t rec_len = dir[pos];
        if (rec_len == 0) {
            // records never straddle sectors; hop to the next one
            pos = (pos / kSector + 1) * kSector;
            continue;
        }
        if (pos + rec_len > dir.size())
            break;
        uint32_t ext_lba = rd_u32(&dir[pos + 2]);
        uint32_t ext_size = rd_u32(&dir[pos + 10]);
        uint8_t flags = dir[pos + 25];
        uint8_t name_len = dir[pos + 32];
        if (pos + 33 + name_len <= dir.size() && name_len > 0) {
            std::string name(reinterpret_cast<char*>(&dir[pos + 33]),
                             name_len);
            if (name != std::string(1, '\0') && name != std::string(1, '\1')) {
                size_t semi = name.find(';');
                if (semi != std::string::npos)
                    name.resize(semi);
                std::string path = prefix + "/" + name;
                if (flags & 2)
                    walk(ext_lba, ext_size, path, depth + 1);
                else
                    files_.push_back({path, ext_lba, ext_size});
            }
        }
        pos += rec_len;
    }
}

const IsoFile* IsoFs::find(const std::string& path) const {
    auto norm = [](std::string s) {
        for (char& c : s) {
            if (c == '\\') c = '/';
            c = char(toupper(uint8_t(c)));
        }
        size_t semi = s.find(';');
        if (semi != std::string::npos)
            s.resize(semi);
        if (!s.empty() && s[0] != '/')
            s.insert(s.begin(), '/');
        return s;
    };
    std::string want = norm(path);
    for (const auto& f : files_)
        if (norm(f.path) == want)
            return &f;
    return nullptr;
}

std::vector<uint8_t> IsoFs::read(const IsoFile& f) const {
    std::vector<uint8_t> out;
    if (!fp_)
        return out;
    FILE* fp = static_cast<FILE*>(fp_);
    out.resize(f.size);
    if (_fseeki64(fp, int64_t(f.lba) * kSector, SEEK_SET) != 0 ||
        fread(out.data(), 1, f.size, fp) != f.size)
        out.clear();
    return out;
}

std::vector<uint8_t> IsoFs::read(const std::string& path) const {
    const IsoFile* f = find(path);
    return f ? read(*f) : std::vector<uint8_t>{};
}

std::vector<uint8_t> IsoFs::read_range(const IsoFile& f, uint32_t offset,
                                       uint32_t length) const {
    std::vector<uint8_t> out;
    if (!fp_ || offset >= f.size)
        return out;
    if (offset + length > f.size)
        length = f.size - offset;
    FILE* fp = static_cast<FILE*>(fp_);
    out.resize(length);
    if (_fseeki64(fp, int64_t(f.lba) * kSector + offset, SEEK_SET) != 0 ||
        fread(out.data(), 1, length, fp) != length)
        out.clear();
    return out;
}

// ---- TJZIP --------------------------------------------------------------------

namespace {

constexpr size_t kHistory = 0x10000;   // zero-filled LZ history window

struct TjState {
    const uint8_t* in;
    size_t in_len, in_pos = 0;
    std::vector<uint8_t>& buf;   // kHistory zeros + payload
    size_t out_pos = kHistory;
    size_t out_end;              // kHistory + decomp_size

    bool full() const { return out_pos >= out_end; }
};

bool tj_raw(TjState& s, std::string* err) {
    if (s.in_pos >= s.in_len) {
        if (err) *err = "tjzip: raw block past end of input";
        return false;
    }
    uint8_t b0 = s.in[s.in_pos++];
    size_t run;
    if (b0 != 0) {
        run = size_t(b0) + 2;
    } else {
        if (s.in_pos + 2 > s.in_len) {
            if (err) *err = "tjzip: raw u16 length past end of input";
            return false;
        }
        run = size_t(s.in[s.in_pos]) | (size_t(s.in[s.in_pos + 1]) << 8);
        s.in_pos += 2;
    }
    if (s.out_pos + run > s.buf.size() || s.in_pos + run > s.in_len) {
        if (err) *err = "tjzip: raw block overrun";
        return false;
    }
    std::memcpy(s.buf.data() + s.out_pos, s.in + s.in_pos, run);
    s.in_pos += run;
    s.out_pos += run;
    return true;
}

bool tj_dict(TjState& s, int& post, std::string* err) {
    if (s.in_pos + 2 > s.in_len) {
        if (err) *err = "tjzip: dict code past end of input";
        return false;
    }
    uint8_t b1 = s.in[s.in_pos];
    uint8_t b2 = s.in[s.in_pos + 1];
    s.in_pos += 2;
    size_t length, dist;
    uint8_t top = b1 & 0xE0;
    if (top < 0xC0) {
        length = size_t(b1 >> 5) + 4;
        dist = ((size_t(b1) << 6) & 0x300) | b2;
        post = b1 & 3;
    } else if (top == 0xC0) {
        if (s.in_pos >= s.in_len) {
            if (err) *err = "tjzip: dict(C0) past end of input";
            return false;
        }
        uint8_t b3 = s.in[s.in_pos++];
        length = size_t(b1 & 0x1F) + 4;
        dist = ((size_t(b2) << 6) & 0x3F00) | b3;
        post = b2 & 3;
    } else {
        if (s.in_pos >= s.in_len) {
            if (err) *err = "tjzip: dict(E0) past end of input";
            return false;
        }
        uint8_t b3 = s.in[s.in_pos++];
        size_t small_len = size_t(b1 & 0x0F) + 3;
        if (small_len >= 4) {
            length = small_len;
            dist = ((size_t(b1 & 0x10) << 10)) | ((size_t(b2 & 0xFC) << 6)) |
                   b3;
            post = b2 & 3;
        } else {
            if (s.in_pos >= s.in_len) {
                if (err) *err = "tjzip: dict(E0/ext) past end of input";
                return false;
            }
            uint8_t b4 = s.in[s.in_pos++];
            size_t len2 = size_t(b2) + 0x12;
            if (len2 >= 0x13) {
                length = len2;
                dist = ((size_t(b1 & 0x10) << 10)) |
                       ((size_t(b3 & 0xFC) << 6)) | b4;
                post = b3 & 3;
            } else {
                if (s.in_pos + 2 > s.in_len) {
                    if (err) *err = "tjzip: dict(E0/veryext) past end";
                    return false;
                }
                uint8_t b5 = s.in[s.in_pos];
                uint8_t b6 = s.in[s.in_pos + 1];
                s.in_pos += 2;
                length = (size_t(b3) << 8) | b4;
                dist = ((size_t(b1 & 0x10) << 10)) |
                       ((size_t(b5 & 0xFC) << 6)) | b6;
                post = b5 & 3;
            }
        }
    }
    if (dist == 0 || dist > s.out_pos) {
        if (err) *err = "tjzip: bad backreference distance";
        return false;
    }
    if (s.out_pos + length > s.buf.size()) {
        if (err) *err = "tjzip: dict copy overrun";
        return false;
    }
    // byte-wise: overlapping copies replicate (classic LZ)
    size_t src = s.out_pos - dist;
    for (size_t i = 0; i < length; i++)
        s.buf[s.out_pos + i] = s.buf[src + i];
    s.out_pos += length;
    return true;
}

}  // namespace

bool tjzip_decompress(std::span<const uint8_t> comp, size_t decomp_size,
                      std::vector<uint8_t>& out, std::string* err, size_t* consumed) {
    std::vector<uint8_t> buf(kHistory + decomp_size, 0);
    TjState s{comp.data(), comp.size(), 0, buf, kHistory,
              kHistory + decomp_size};
    if (!tj_raw(s, err))
        return false;
    while (!s.full() && s.in_pos < s.in_len) {
        int post = 3;
        if (!tj_dict(s, post, err))
            return false;
        if (s.full())
            break;   // the last post-op may promise data the stream lacks
        if (post == 0) {
            if (!tj_raw(s, err))
                return false;
        } else if (post == 1 || post == 2) {
            if (s.in_pos + post > s.in_len ||
                s.out_pos + post > s.buf.size()) {
                if (err) *err = "tjzip: post-literal overrun";
                return false;
            }
            for (int i = 0; i < post; i++)
                s.buf[s.out_pos++] = s.in[s.in_pos++];
        }
    }
    if (s.out_pos != s.out_end) {
        if (err)
            *err = "tjzip: stream ended " +
                   std::to_string(s.out_end - s.out_pos) + " bytes short";
        return false;
    }
    out.assign(buf.begin() + kHistory, buf.end());
    if(consumed)*consumed=s.in_pos;
    return true;
}

// ---- HIG! WAD -----------------------------------------------------------------

uint32_t wad_checksum(std::span<const uint8_t> image) {
    static const auto table=[](){std::array<uint32_t,256> t{};for(uint32_t i=0;i<256;i++){
        uint32_t c=i;for(int k=0;k<8;k++)c=(c&1)?0xedb88320u^(c>>1):c>>1;t[i]=c;}return t;}();
    uint32_t crc=0;for(uint8_t b:image)crc=table[(crc^b)&255]^(crc>>8);return ~crc;
}

bool parse_hig_header(std::span<const uint8_t> wad, HigWad& out,
                      std::string* err) {
    if (wad.size() < 0xC0 || std::memcmp(wad.data(), "HIG!", 4) != 0) {
        if (err) *err = "not a HIG! wad";
        return false;
    }
    for (int i = 0; i < kHigSectionSlots; i++)
        out.sections[i] = rd_u32(wad.data() + 4 + i * 4);
    out.crc = rd_u32(wad.data() + 0x34);
    out.version = rd_u32(wad.data() + 0x38);
    out.decomp_size = rd_u32(wad.data() + 0x3C);
    out.comp_size = rd_u32(wad.data() + 0x40);
    const char* p = reinterpret_cast<const char*>(wad.data() + 0x44);
    size_t n = 0;
    while (n < 0x7C && p[n])
        n++;
    out.source_path.assign(p, n);
    // header-only spans (exactly 0xC0 bytes) are fine for metadata reads;
    // a full-file span must carry the stream the header promises
    if (wad.size() > 0xC0 && out.comp_size > wad.size() - 0xC0) {
        if (err)
            *err = "HIG! comp_size mismatch (" +
                   std::to_string(out.comp_size) + " vs " +
                   std::to_string(wad.size() - 0xC0) + " available)";
        return false;
    }
    return true;
}

bool unpack_hig_wad(std::span<const uint8_t> wad, HigWad& hdr,
                    std::vector<uint8_t>& image, std::string* err) {
    if (!parse_hig_header(wad, hdr, err))
        return false;
    if (hdr.comp_size > wad.size()-0xC0) { if(err)*err="HIG! compressed payload is missing";return false; }
    return tjzip_decompress(wad.subspan(0xC0, hdr.comp_size), hdr.decomp_size, image, err);
}

// ---- PAKK ---------------------------------------------------------------------

bool parse_pak(std::span<const uint8_t> pak, std::vector<PakEntry>& out,
               std::string* err) {
    if (pak.size() < 0x10 || std::memcmp(pak.data(), "PAKK", 4) != 0) {
        if (err) *err = "not a PAKK container";
        return false;
    }
    uint32_t count = rd_u32(pak.data() + 0x0C);
    if (0x10 + size_t(count) * 8 > pak.size()) {
        if (err) *err = "PAKK entry table out of bounds";
        return false;
    }
    out.clear();
    for (uint32_t i = 0; i < count; i++) {
        PakEntry e;
        e.offset = rd_u32(pak.data() + 0x10 + i * 8);
        e.size = rd_u32(pak.data() + 0x10 + i * 8 + 4);
        if (size_t(e.offset) + e.size > pak.size()) {
            if (err) *err = "PAKK entry " + std::to_string(i) + " out of bounds";
            return false;
        }
        out.push_back(e);
    }
    return true;
}

bool looks_like_gim(std::span<const uint8_t> bytes) {
    return bytes.size() >= 11 &&
           std::memcmp(bytes.data(), "MIG.00.1PSP", 11) == 0;
}

}  // namespace hig
