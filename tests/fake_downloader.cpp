#include <cstdlib>
#include <fstream>
#include <string>
#include <filesystem>
#include <iostream>
#include <vector>
#include <iomanip>
#include <sstream>
#include <regex>
#include <chrono>
#include <thread>
int finish(const std::vector<std::string>& args) {
    if (const char* count_text = std::getenv("YTPLAYLIST_TEST_COUNT")) {
        const int count = std::atoi(count_text);
        std::vector<int> selected;
        bool fast_options = false;
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--playlist-end" || args[i] == "--max-filesize") return 9;
            if (args[i] == "--extractor-retries" && i + 1 < args.size()) fast_options = args[i + 1] == "0";
            if (args[i] == "--load-info-json" && i + 1 < args.size()) {
                std::ifstream manifest(std::filesystem::u8path(args[i + 1]));
                const std::string contents(std::istreambuf_iterator<char>(manifest), {});
                const std::regex urls("https://www\\.youtube\\.com/watch\\?v=vid([0-9]+)");
                for (std::sregex_iterator it(contents.begin(), contents.end(), urls), end; it != end; ++it)
                    selected.push_back(std::stoi((*it)[1]));
            }
        }
        for (size_t i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == "--print-to-file" && i + 2 < args.size()) {
                std::ofstream list(std::filesystem::u8path(args[i + 2]), std::ios::binary);
                for (int item = 1; item <= count; ++item) {
                    const bool mixed = std::getenv("YTPLAYLIST_TEST_UNAVAILABLE");
                    const auto title = mixed && item == 2 ? "[Private video]" :
                        mixed && item == 4 ? "[Deleted video]" : "Test " + std::to_string(item);
                    list << "{\"id\":\"vid" << item
                         << "\",\"playlist_title\":\"Long playlist\",\"playlist_index\":" << item
                         << ",\"duration\":" << item * 10 << ",\"title\":\"" << title << "\"}\n";
                }
            }
            if (args[i] == "--paths") {
                if (selected.empty() || !fast_options) return 9;
                for (int item : selected) {
                    if (std::getenv("YTPLAYLIST_TEST_UNAVAILABLE") && (item == 2 || item == 4)) return 9;
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
                const auto filename = std::getenv("YTPLAYLIST_TEST_SINGLE") ?
                    "NA - test [abc].mp3" : "001 - test [abc].mp3";
                std::ofstream media(std::filesystem::u8path(args[i + 1]) / filename, std::ios::binary);
                media << "test media payload";
                std::cout << "[youtube] abc: Downloading webpage\n[download] Downloading item 1 of 1\n"
                             "[download] 100.0%\n" << std::flush;
            }
            if (args[i] == "--print-to-file" && i + 2 < args.size()) {
                const char* wait_file = std::getenv("YTPLAYLIST_TEST_WAIT_FILE");
                for (int attempt = 0; wait_file && !std::filesystem::exists(wait_file) && attempt < 500; ++attempt)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                std::ofstream list(std::filesystem::u8path(args[i + 2]), std::ios::binary);
                if (std::getenv("YTPLAYLIST_TEST_SINGLE")) {
                    // A direct video extract has no playlist title or position.
                    list << R"({"id": "abc", "title": "Test", "duration": 19})" << '\n';
                } else {
                    list << R"({"id": "abc", "title": "Test", "duration": 19, "playlist_title": "List"})" << '\n'
                         << R"({"id": "gone", "title": "Missing", "duration": null, "playlist_title": "List"})" << '\n';
                }
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
