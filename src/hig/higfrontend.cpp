#include "higfrontend.h"
#include <cstring>
namespace hig {
bool decode_frontend_images(std::span<const uint8_t> bytes, std::vector<FrontendImage> &out,
                            std::string *error) {
    out.clear();
    if (bytes.size() < 60 || std::memcmp(bytes.data(), "SNR2", 4)) {
        if (error)
            *error = "Frontend data expects an SNR2 module";
        return false;
    }
    auto u32 = [&](size_t off) {
        uint32_t n;
        std::memcpy(&n, bytes.data() + off, 4);
        return n;
    };
    for (size_t off = 60; off + 32 <= bytes.size(); off += 4) {
        if (std::memcmp(bytes.data() + off, "MIG.00.1PSP", 11))
            continue;
        uint32_t root = u32(off + 16), size = u32(off + 20);
        if (root != 2 || size < 16 || size > bytes.size() - off - 16) {
            if (error)
                *error = "Embedded frontend GIM exceeds its module";
            out.clear();
            return false;
        }
        FrontendImage image;
        image.file_offset = uint32_t(off);
        image.byte_size = size + 16;
        size_t meta = off + image.byte_size;
        // GIM file-information block follows the picture root. Its source
        // path is evidence for asset identity, not a host filesystem path.
        if (meta + 16 <= bytes.size() && u32(meta) == 255) {
            uint32_t n = u32(meta + 4), data = u32(meta + 12);
            if (n >= 16 && n <= bytes.size() - meta && data >= 16 && data < n) {
                const char *name = (const char *)bytes.data() + meta + data;
                if (const void *end = std::memchr(name, 0, n - data))
                    image.source_path.assign(name, (const char *)end - name);
                image.byte_size += n;
            }
        }
        if (!decode_gim(bytes.subspan(off, image.byte_size), image.image, error)) {
            out.clear();
            return false;
        }
        out.push_back(std::move(image));
        off += out.back().byte_size - 4;
        // Shipped containers and metadata are 4-byte aligned.
        if ((off + 4) % 4)
            off = ((off + 4) + 3) / 4 * 4 - 4;
    }
    if (out.empty()) {
        if (error)
            *error = "No embedded frontend GIM images found";
        return false;
    }
    if (error)
        error->clear();
    return true;
}
} // namespace hig
