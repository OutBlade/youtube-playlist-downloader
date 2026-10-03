#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

void help() {
    std::cout << "ytplaylist 1.0.0 - simple YouTube playlist downloader\n\n"
                 "Usage: ytplaylist [options] \"PLAYLIST_URL\"\n\n"
                 "  -o, --output DIR   Download folder (default: downloads)\n"
                 "  -j, --jobs N       Concurrent fragments, 1-32 (default: 8)\n"
                 "  --audio            Extract MP3 audio (requires ffmpeg)\n"
                 "  --yt-dlp PATH      yt-dlp executable (default: yt-dlp on PATH)\n"
                 "  -h, --help         Show this help\n\n"
                 "Requires yt-dlp; ffmpeg enables best-quality video merging.\n";
}

#ifdef _WIN32
std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                  static_cast<int>(value.size()), nullptr, 0);
    if (!size) throw std::runtime_error("Invalid UTF-8 argument");
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), size);
    return result;
}

// Windows CRT quoting: preserve quotes and backslashes without invoking a shell.
std::wstring quote(const std::wstring& arg) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        slashes = 0;
        if (c == L'"') result += L'\\';
        result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
#endif

int run(const std::vector<std::string>& args) {
#ifdef _WIN32
    std::wstring command;
    for (const auto& arg : args) {
        if (!command.empty()) command += L' ';
        command += quote(wide(arg));
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, 0,
                        nullptr, nullptr, &startup, &process)) {
        throw std::runtime_error("Could not start yt-dlp (Windows error " +
            std::to_string(GetLastError()) + "). Install yt-dlp or use --yt-dlp PATH.");
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD status = 1;
    GetExitCodeProcess(process.hProcess, &status);
    CloseHandle(process.hProcess);
    return static_cast<int>(status);
#else
    std::vector<char*> argv;
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    pid_t child = fork();
    if (child < 0) throw std::runtime_error("Could not create downloader process");
    if (child == 0) {
        execvp(argv[0], argv.data());
        std::cerr << "Could not start yt-dlp: " << std::strerror(errno)
                  << ". Install yt-dlp or use --yt-dlp PATH.\n";
        _exit(127);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) throw std::runtime_error("Could not wait for downloader");
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
#endif
}

bool youtube_url(const std::string& url) {
    const auto start = url.rfind("https://", 0) == 0 ? 8u :
                       url.rfind("http://", 0) == 0 ? 7u : 0u;
    if (!start) return false;
    const auto end = url.find_first_of("/?#", start);
    const auto host = url.substr(start, end - start);
    return (host == "youtube.com" || host == "www.youtube.com" ||
            host == "m.youtube.com" || host == "music.youtube.com" ||
            host == "youtu.be") && url.find_first_of("\r\n") == std::string::npos;
}

int app(const std::vector<std::string>& input) {
    try {
        std::string output = "downloads", jobs = "8", engine = "yt-dlp", url;
        bool audio = false;
        if (input.empty()) { help(); return 2; }
        for (size_t i = 0; i < input.size(); ++i) {
            const auto& arg = input[i];
            auto value = [&]() -> std::string {
                if (++i >= input.size() || input[i].empty())
                    throw std::runtime_error("Missing value for " + arg);
                return input[i];
            };
            if (arg == "--help" || arg == "-h") { help(); return 0; }
            else if (arg == "--output" || arg == "-o") output = value();
            else if (arg == "--jobs" || arg == "-j") jobs = value();
            else if (arg == "--yt-dlp") engine = value();
            else if (arg == "--audio") audio = true;
            else if (!arg.empty() && arg[0] == '-')
                throw std::runtime_error("Unknown option: " + arg);
            else if (url.empty()) url = arg;
            else throw std::runtime_error("Provide exactly one playlist URL");
        }
        if (!youtube_url(url)) throw std::runtime_error("Provide a valid YouTube URL");
        if (jobs.empty() || jobs.size() > 2 || jobs.find_first_not_of("0123456789") != std::string::npos ||
            std::stoi(jobs) < 1 || std::stoi(jobs) > 32)
            throw std::runtime_error("--jobs must be a number from 1 to 32");

        fs::path folder = fs::absolute(fs::u8path(output));
        fs::create_directories(folder);
        std::vector<std::string> args = {engine, "--ignore-config", "--yes-playlist",
            "--no-abort-on-error", "--continue", "--no-overwrites",
            "--concurrent-fragments", jobs, "--download-archive",
            (folder / "downloaded.txt").u8string(), "--paths", folder.u8string(),
            "--output", "%(playlist_title)s/%(playlist_index)03d - %(title)s [%(id)s].%(ext)s"};
        if (audio) args.insert(args.end(), {"--format", "bestaudio/best",
            "--extract-audio", "--audio-format", "mp3"});
        args.insert(args.end(), {"--", url});
        std::cout << "Downloading to " << folder.u8string() << '\n' << std::flush;
        int status = run(args);
        if (status != 0) std::cerr << "Downloader exited with code " << status
            << ". Some items may have failed; rerun to resume.\n";
        return status;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 2;
    }
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        int size = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string arg(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, arg.data(), size, nullptr, nullptr);
        arg.pop_back();
        args.push_back(arg);
    }
    return app(args);
}
#else
int main(int argc, char** argv) {
    return app(std::vector<std::string>(argv + 1, argv + argc));
}
#endif
