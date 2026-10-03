#include <cstdlib>
#include <fstream>
#include <string>
#ifdef _WIN32
#include <windows.h>
int wmain(int argc, wchar_t** argv) {
    std::ofstream log(std::getenv("YTPLAYLIST_TEST_LOG"), std::ios::binary);
    for (int i = 1; i < argc; ++i) {
        int n = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string arg(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, arg.data(), n, nullptr, nullptr);
        arg.pop_back();
        log << arg << '\n';
    }
#else
int main(int argc, char** argv) {
    std::ofstream log(std::getenv("YTPLAYLIST_TEST_LOG"), std::ios::binary);
    for (int i = 1; i < argc; ++i) log << argv[i] << '\n';
#endif
    const char* status = std::getenv("YTPLAYLIST_TEST_EXIT");
    return status ? std::atoi(status) : 0;
}
