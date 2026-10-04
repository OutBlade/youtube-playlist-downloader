#include <cstdlib>
#include <fstream>
#include <string>
#include <filesystem>
#include <iostream>
#include <vector>
#include <iomanip>
#include <sstream>
int finish(const std::vector<std::string>& args) {
    if (const char* count_text = std::getenv("YTPLAYLIST_TEST_COUNT")) {
        const int count = std::atoi(count_text);
        int first = 1, step = 1;
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--playlist-end" || args[i] == "--max-filesize") return 9;
            if (args[i] == "--playlist-items" && i + 1 < args.size()) {
                first = std::stoi(args[i + 1]);
                const auto divider = args[i + 1].find("::");
                if (divider == std::string::npos) return 9;
                step = std::stoi(args[i + 1].substr(divider + 2));
            }
        }
        for (size_t i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == "--print-to-file" && i + 2 < args.size()) {
                std::ofstream list(std::filesystem::u8path(args[i + 2]), std::ios::binary);
                for (int item = 1; item <= count; ++item)
                    list << "{\"id\":\"vid" << item << "\",\"title\":\"Test " << item
                         << "\",\"playlist_title\":\"Long playlist\"}\n";
            }
            if (args[i] == "--paths") {
                for (int item = first; item <= count; item += step) {
                    std::ostringstream name;
                    name << std::setw(3) << std::setfill('0') << item << " - test [vid" << item << "].mp3";
                    std::ofstream media(std::filesystem::u8path(args[i + 1]) / name.str(), std::ios::binary);
                    media << "test media payload";
                }
                std::cout << "[download] 100.0%\n" << std::flush;
            }
        }
        return 0;
    }
    if (std::getenv("YTPLAYLIST_TEST_MEDIA")) {
        for (size_t i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == "--paths") {
                std::ofstream media(std::filesystem::u8path(args[i + 1]) / "001 - test [abc].mp3", std::ios::binary);
                media << "test media payload";
                std::cout << "[youtube] abc: Downloading webpage\n[download] Downloading item 1 of 1\n"
                             "[download] 100.0%\n" << std::flush;
            }
            if (args[i] == "--print-to-file" && i + 2 < args.size()) {
                std::ofstream list(std::filesystem::u8path(args[i + 2]), std::ios::binary);
                list << R"({"id": "abc", "title": "Test", "duration": 19, "playlist_title": "List"})" << '\n'
                     << R"({"id": "gone", "title": "Missing", "duration": null, "playlist_title": "List"})" << '\n';
            }
        }
    }
    const char* status = std::getenv("YTPLAYLIST_TEST_EXIT");
    return status ? std::atoi(status) : 0;
}
#ifdef _WIN32
#include <windows.h>
int wmain(int argc, wchar_t** argv) {
    std::ofstream log(std::getenv("YTPLAYLIST_TEST_LOG"), std::ios::binary);
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        int n = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string arg(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, arg.data(), n, nullptr, nullptr);
        arg.pop_back();
        log << arg << '\n';
        args.push_back(arg);
    }
#else
int main(int argc, char** argv) {
    std::ofstream log(std::getenv("YTPLAYLIST_TEST_LOG"), std::ios::binary);
    std::vector<std::string> args(argv + 1, argv + argc);
    for (int i = 1; i < argc; ++i) log << argv[i] << '\n';
#endif
    return finish(args);
}
