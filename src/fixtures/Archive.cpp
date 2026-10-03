#include "fixtures/Archive.h"

#include <miniz.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>

namespace dmxviz::fixtures {

struct ZipReader::Impl {
    mz_zip_archive zip{};
    bool open = false;
};

ZipReader::ZipReader() : impl_(std::make_unique<Impl>()) {}

ZipReader::~ZipReader() {
    if (impl_ && impl_->open) mz_zip_reader_end(&impl_->zip);
}

bool ZipReader::open(std::vector<std::uint8_t> bytes, std::string* error) {
    if (impl_->open) {
        mz_zip_reader_end(&impl_->zip);
        impl_->open = false;
    }
    entries_.clear();
    bytes_ = std::move(bytes);
    std::memset(&impl_->zip, 0, sizeof(impl_->zip));
    if (bytes_.empty() || !mz_zip_reader_init_mem(&impl_->zip, bytes_.data(), bytes_.size(), 0)) {
        if (error) *error = "not a zip archive";
        return false;
    }
    impl_->open = true;
    const mz_uint count = mz_zip_reader_get_num_files(&impl_->zip);
    for (mz_uint i = 0; i < count; ++i) {
        if (mz_zip_reader_is_file_a_directory(&impl_->zip, i)) continue;
        mz_zip_archive_file_stat stat{};
        if (mz_zip_reader_file_stat(&impl_->zip, i, &stat)) entries_.emplace_back(stat.m_filename);
    }
    return true;
}

bool ZipReader::openFile(const std::filesystem::path& path, std::string* error) {
    auto bytes = readFileBytes(path, error);
    if (!bytes) return false;
    return open(std::move(*bytes), error);
}

bool ZipReader::contains(std::string_view name) const {
    return std::find(entries_.begin(), entries_.end(), name) != entries_.end();
}

std::optional<std::string> ZipReader::findCaseInsensitive(std::string_view name) const {
    auto lowerEq = [](std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        return true;
    };
    for (const std::string& e : entries_)
        if (lowerEq(e, name)) return e;
    return std::nullopt;
}

std::optional<std::vector<std::uint8_t>> ZipReader::read(std::string_view name) const {
    if (!impl_->open) return std::nullopt;
    const std::string key(name);
    const int index = mz_zip_reader_locate_file(&impl_->zip, key.c_str(), nullptr, 0);
    if (index < 0) return std::nullopt;
    std::size_t size = 0;
    void* data = mz_zip_reader_extract_to_heap(&impl_->zip, static_cast<mz_uint>(index), &size, 0);
    if (!data) return std::nullopt;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::vector<std::uint8_t> out(bytes, bytes + size);
    mz_free(data);
    return out;
}

void ZipWriter::add(std::string name, std::span<const std::uint8_t> data) {
    files_.emplace_back(std::move(name), std::vector<std::uint8_t>(data.begin(), data.end()));
}

void ZipWriter::add(std::string name, std::string_view text) {
    files_.emplace_back(std::move(name), std::vector<std::uint8_t>(text.begin(), text.end()));
}

std::vector<std::uint8_t> ZipWriter::finish() const {
    mz_zip_archive zip{};
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) return {};
    for (const auto& [name, data] : files_) {
        if (!mz_zip_writer_add_mem(&zip, name.c_str(), data.data(), data.size(), static_cast<mz_uint>(MZ_DEFAULT_LEVEL))) {
            mz_zip_writer_end(&zip);
            return {};
        }
    }
    void* buffer = nullptr;
    std::size_t size = 0;
    std::vector<std::uint8_t> out;
    if (mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size)) {
        const auto* bytes = static_cast<const std::uint8_t*>(buffer);
        out.assign(bytes, bytes + size);
        mz_free(buffer);  // finalize_heap_archive hands the buffer over to the caller
    }
    mz_zip_writer_end(&zip);
    return out;
}

namespace {
constexpr char kBase64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}
}  // namespace

std::string base64Encode(std::span<const std::uint8_t> data) {
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        const std::uint32_t v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out.push_back(kBase64Chars[(v >> 18) & 63]);
        out.push_back(kBase64Chars[(v >> 12) & 63]);
        out.push_back(kBase64Chars[(v >> 6) & 63]);
        out.push_back(kBase64Chars[v & 63]);
    }
    const std::size_t rest = data.size() - i;
    if (rest == 1) {
        const std::uint32_t v = data[i] << 16;
        out.push_back(kBase64Chars[(v >> 18) & 63]);
        out.push_back(kBase64Chars[(v >> 12) & 63]);
        out += "==";
    } else if (rest == 2) {
        const std::uint32_t v = (data[i] << 16) | (data[i + 1] << 8);
        out.push_back(kBase64Chars[(v >> 18) & 63]);
        out.push_back(kBase64Chars[(v >> 12) & 63]);
        out.push_back(kBase64Chars[(v >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> base64Decode(std::string_view text) {
    std::vector<std::uint8_t> out;
    out.reserve(text.size() / 4 * 3);
    std::uint32_t acc = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=') break;
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        const int v = base64Value(c);
        if (v < 0) return std::nullopt;
        acc = (acc << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xFFu));
        }
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> readFileBytes(const std::filesystem::path& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + path.string();
        return std::nullopt;
    }
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool writeFileBytes(const std::filesystem::path& path, std::span<const std::uint8_t> data, std::string* error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) *error = "cannot write " + path.string();
        return false;
    }
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out) {
        if (error) *error = "write failed: " + path.string();
        return false;
    }
    return true;
}

}  // namespace dmxviz::fixtures
