#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace hig {

// High Impact Games engine (R&C: Size Matters / Secret Agent Clank,
// PS2 + PSP). ISO9660 disc reader + HIG! WAD (TJZIP) + PAKK containers.
// Byte layouts: docs/HIG_FORMATS.md (reversed from the retail PS2 ISO and
// the game's own TJZIP symbols). Everything little-endian.

// ---- ISO9660 (plain 2048-byte sectors, primary volume descriptor) ----------

struct IsoFile {
    std::string path;    // "/WAD/17.WAD"
    uint32_t lba = 0;    // extent start (2048-byte sectors)
    uint32_t size = 0;   // bytes
};

class IsoFs {
public:
    IsoFs() = default;
    ~IsoFs() { close(); }
    IsoFs(const IsoFs&) = delete;
    IsoFs& operator=(const IsoFs&) = delete;
    // Opens the image and walks the primary directory tree.
    bool open(const std::wstring& iso_path, std::string* err = nullptr);
    void close();
    bool is_open() const { return fp_ != nullptr; }

    const std::string& volume_id() const { return volume_id_; }
    const std::vector<IsoFile>& files() const { return files_; }
    // Case-insensitive lookup ("/WAD/17.WAD"; ";1" suffixes are stripped).
    const IsoFile* find(const std::string& path) const;
    // Whole-file read (empty on failure).
    std::vector<uint8_t> read(const IsoFile& f) const;
    std::vector<uint8_t> read(const std::string& path) const;
    // Partial read (headers etc.) — clamped to the file's extent.
    std::vector<uint8_t> read_range(const IsoFile& f, uint32_t offset,
                                    uint32_t length) const;

private:
    void walk(uint32_t lba, uint32_t size, const std::string& prefix,
              int depth);
    void* fp_ = nullptr;   // FILE*
    std::string volume_id_;
    std::vector<IsoFile> files_;
};

// ---- TJZIP ------------------------------------------------------------------

// Decompress a TJZIP stream to exactly decomp_size bytes. Distances may
// reference (zero) history before the output start; decoding stops the
// moment the output is full (the final post-op may promise data the stream
// does not contain). Returns false + err on malformed input.
bool tjzip_decompress(std::span<const uint8_t> comp, size_t decomp_size,
                      std::vector<uint8_t>& out, std::string* err = nullptr,
                      size_t* consumed = nullptr);
// Retail TJZIP CRC: reflected CRC32, initial register 0, final complement.
uint32_t wad_checksum(std::span<const uint8_t> image);

// ---- HIG! WAD (version 6) ----------------------------------------------------

constexpr int kHigSectionSlots = 12;

struct HigWad {
    uint32_t sections[kHigSectionSlots]{};  // offsets in the decompressed
                                            // image, 0 = slot absent
    uint32_t crc = 0;
    uint32_t version = 0;                   // 6
    uint32_t decomp_size = 0;
    uint32_t comp_size = 0;                 // compressed stream bytes; ISO extent may have padding
    std::string source_path;                // dev .wad.conf path
};

// Parse the fixed 0xC0 header (no decompression).
bool parse_hig_header(std::span<const uint8_t> wad, HigWad& out,
                      std::string* err = nullptr);
// Header + full TJZIP decompression of the image.
bool unpack_hig_wad(std::span<const uint8_t> wad, HigWad& hdr,
                    std::vector<uint8_t>& image, std::string* err = nullptr);

// ---- PAKK ---------------------------------------------------------------------

struct PakEntry {
    uint32_t offset = 0, size = 0;
};

bool parse_pak(std::span<const uint8_t> pak, std::vector<PakEntry>& out,
               std::string* err = nullptr);

// Standard Sony GIM texture sniff ("MIG.00.1PSP").
bool looks_like_gim(std::span<const uint8_t> bytes);

}  // namespace hig
