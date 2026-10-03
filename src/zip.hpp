#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

// Stored ZIP entries stream to disk. No shell, compression library, or RAM-sized media buffer.
inline void make_zip(const std::filesystem::path& destination,
                     const std::vector<std::filesystem::path>& files) {
    struct Entry { std::string name; uint32_t crc, size, offset; };
    std::vector<Entry> entries;
    std::ofstream out(destination, std::ios::binary);
    auto put = [&](uint32_t value, int bytes) {
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
    uint64_t total = 0;
    for (const auto& file : files) {
        const auto size = std::filesystem::file_size(file);
        total += size;
        if (total > 2ull * 1024 * 1024 * 1024) throw std::runtime_error("Playlist exceeds 2 GB limit");
        Entry entry{file.filename().u8string(), 0, static_cast<uint32_t>(size),
                    static_cast<uint32_t>(out.tellp())};
        uint32_t crc = 0xffffffff;
        std::ifstream input(file, std::ios::binary);
        while (input.read(buffer.data(), buffer.size()) || input.gcount()) {
            for (std::streamsize i = 0; i < input.gcount(); ++i) {
                crc = (crc >> 8) ^ table[(crc ^ static_cast<unsigned char>(buffer[static_cast<size_t>(i)])) & 255];
            }
        }
        entry.crc = crc ^ 0xffffffff;
        put(0x04034b50, 4); put(20, 2); put(0x800, 2); put(0, 2);
        put(0, 2); put(0x21, 2); put(entry.crc, 4); put(entry.size, 4); put(entry.size, 4);
        put(static_cast<uint32_t>(entry.name.size()), 2); put(0, 2);
        out << entry.name;
        input.clear(); input.seekg(0);
        while (input.read(buffer.data(), buffer.size()) || input.gcount()) out.write(buffer.data(), input.gcount());
        entries.push_back(entry);
    }
    const auto directory = static_cast<uint32_t>(out.tellp());
    for (const auto& entry : entries) {
        put(0x02014b50, 4); put(20, 2); put(20, 2); put(0x800, 2); put(0, 2);
        put(0, 2); put(0x21, 2); put(entry.crc, 4); put(entry.size, 4); put(entry.size, 4);
        put(static_cast<uint32_t>(entry.name.size()), 2); put(0, 2); put(0, 2); put(0, 2);
        put(0, 2); put(0, 4); put(entry.offset, 4); out << entry.name;
    }
    const auto end = static_cast<uint32_t>(out.tellp());
    put(0x06054b50, 4); put(0, 2); put(0, 2);
    put(static_cast<uint32_t>(entries.size()), 2); put(static_cast<uint32_t>(entries.size()), 2);
    put(end - directory, 4); put(directory, 4); put(0, 2);
    out.close();
    if (!out) throw std::runtime_error("Could not write playlist ZIP");
}
