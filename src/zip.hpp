#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

// Stored ZIP64 entries stream to disk, including 64-bit sizes, offsets and entry counts.
inline void make_zip(const std::filesystem::path& destination,
                     const std::vector<std::filesystem::path>& files) {
    struct Entry { std::string name; uint32_t crc; uint64_t size, offset; };
    std::vector<Entry> entries;
    std::ofstream out(destination, std::ios::binary);
    auto put = [&](uint64_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) out.put(static_cast<char>(value >> (i * 8)));
    };
    std::array<char, 65536> buffer{};
    static const auto table = [] {
        std::array<uint32_t, 256> values{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1)));
            values[i] = value;
        }
        return values;
    }();
    for (const auto& file : files) {
        const auto size = std::filesystem::file_size(file);
        Entry entry{file.filename().u8string(), 0, size, static_cast<uint64_t>(out.tellp())};
        if (entry.name.size() > 65535) throw std::runtime_error("Filename is too long for ZIP");
        uint32_t crc = 0xffffffff;
        std::ifstream input(file, std::ios::binary);
        if (!input) throw std::runtime_error("Could not read downloaded file");
        while (input.read(buffer.data(), buffer.size()) || input.gcount()) {
            for (std::streamsize i = 0; i < input.gcount(); ++i) {
                crc = (crc >> 8) ^ table[(crc ^ static_cast<unsigned char>(buffer[static_cast<size_t>(i)])) & 255];
            }
        }
        entry.crc = crc ^ 0xffffffff;
        if (!input.eof()) throw std::runtime_error("Could not read downloaded file");
        put(0x04034b50, 4); put(45, 2); put(0x800, 2); put(0, 2);
        put(0, 2); put(0x21, 2); put(entry.crc, 4); put(0xffffffff, 4); put(0xffffffff, 4);
        put(entry.name.size(), 2); put(20, 2);
        out << entry.name;
        put(1, 2); put(16, 2); put(entry.size, 8); put(entry.size, 8);
        input.clear(); input.seekg(0);
        while (input.read(buffer.data(), buffer.size()) || input.gcount()) out.write(buffer.data(), input.gcount());
        if (!input.eof() || !out) throw std::runtime_error("Could not write downloaded file to ZIP");
        entries.push_back(entry);
    }
    const auto directory = static_cast<uint64_t>(out.tellp());
    for (const auto& entry : entries) {
        put(0x02014b50, 4); put(45, 2); put(45, 2); put(0x800, 2); put(0, 2);
        put(0, 2); put(0x21, 2); put(entry.crc, 4); put(0xffffffff, 4); put(0xffffffff, 4);
        put(entry.name.size(), 2); put(28, 2); put(0, 2); put(0, 2);
        put(0, 2); put(0, 4); put(0xffffffff, 4); out << entry.name;
        put(1, 2); put(24, 2); put(entry.size, 8); put(entry.size, 8); put(entry.offset, 8);
    }
    const auto end = static_cast<uint64_t>(out.tellp());
    put(0x06064b50, 4); put(44, 8); put(45, 2); put(45, 2); put(0, 4); put(0, 4);
    put(entries.size(), 8); put(entries.size(), 8); put(end - directory, 8); put(directory, 8);
    put(0x07064b50, 4); put(0, 4); put(end, 8); put(1, 4);
    put(0x06054b50, 4); put(0, 2); put(0, 2);
    put(0xffff, 2); put(0xffff, 2); put(0xffffffff, 4); put(0xffffffff, 4); put(0, 2);
    out.close();
    if (!out) throw std::runtime_error("Could not write playlist ZIP");
}
