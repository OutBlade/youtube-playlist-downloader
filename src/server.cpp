#define YTPLAYLIST_NO_MAIN
#include "main.cpp"
#include "zip.hpp"
#include "httplib.h"
#include "json.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;
struct Job {
    std::string id, state = "reading", message, title, visitor;
    std::atomic<bool> cancel{false};
    fs::path folder;
    std::vector<fs::path> files;
    json items = json::array();
    Clock::time_point created = Clock::now();
    int code = -1;
};
bool finished(const Job& job) {
    return job.state == "complete" || job.state == "partial" || job.state == "failed" ||
        job.state == "cancelled";
}
std::string setting(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value && *value ? value : fallback;
}
std::string read_tail(const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    auto size = file.tellg();
    file.seekg(size > 12000 ? size - std::streamoff(12000) : std::streampos(0));
    return std::string(std::istreambuf_iterator<char>(file), {});
}
void reply(httplib::Response& res, int status, const json& data) {
    res.status = status;
    res.set_content(data.dump(-1, ' ', false, json::error_handler_t::replace), "application/json");
    res.set_header("Cache-Control", "no-store");
}
// Item states are read from the downloader's own output.
json item_states(const Job& job) {
    json items = job.items;
    std::map<std::string, size_t> index;
    for (size_t i = 0; i < items.size(); ++i) {
        items[i]["state"] = "queued";
        index[items[i]["id"].get<std::string>()] = i;
    }
    std::ifstream log(job.folder / "progress.log", std::ios::binary);
    json* current = nullptr;
    for (std::string line; std::getline(log, line);) {
        const bool error = line.rfind("ERROR: [youtube] ", 0) == 0;
        if (error || line.rfind("[youtube] ", 0) == 0) {
            const size_t start = error ? 17 : 10;
            const auto end = line.find(": ", start);
            if (end == std::string::npos) continue;
            const auto found = index.find(line.substr(start, end - start));
            if (found == index.end()) continue;
            auto& item = items[found->second];
            if (error) {
                item["state"] = "failed";
                item["note"] = line.find("confirm your age") != std::string::npos ? "Age-restricted" :
                    line.find("rivate") != std::string::npos ? "Private" : "Unavailable";
            } else if (current != &item) {
                if (current && (*current)["state"] == "active") (*current)["state"] = "done";
                current = &item; item["state"] = "active";
            }
        } else if (current && line.rfind("[download] ", 0) == 0) {
            const auto digit = line.find_first_not_of(' ', 11);
            if (line.find("larger than max-filesize") != std::string::npos) {
                (*current)["state"] = "skipped"; (*current)["note"] = "Too large";
            } else if (digit != std::string::npos && std::isdigit(static_cast<unsigned char>(line[digit])) &&
                line.find('%') != std::string::npos) {
                (*current)["percent"] = std::atof(line.c_str() + digit);
            }
        }
    }
    if (!finished(job)) return items;
    for (auto& item : items) {
        const auto tag = "[" + item["id"].get<std::string>() + "].";
        for (size_t i = 0; i < job.files.size(); ++i)
            if (job.files[i].filename().u8string().find(tag) != std::string::npos) item["file"] = i;
        if (item.contains("file")) item["state"] = "done";
        else if (item["state"] != "skipped" && item["state"] != "failed") {
            item["state"] = "failed"; item["note"] = "Unavailable";
        }
    }
    return items;
}
// Behind a reverse proxy every request arrives from the proxy's address.
std::string client(const httplib::Request& req, bool proxied) {
    if (!proxied) return req.remote_addr;
    auto address = req.get_header_value("CF-Connecting-IP");
    if (address.empty()) {
        address = req.get_header_value("X-Forwarded-For");
        address.erase(0, address.find_last_of(", ") + 1);
    }
    return address.empty() ? req.remote_addr : address;
}
std::string identifier() {
    std::random_device random;
    const char* digits = "0123456789abcdef";
    std::string result;
    for (int i = 0; i < 32; ++i) result += digits[random() & 15];
    return result;
}

int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--help") {
            std::cout << "ytplaylist-web - browser playlist downloader\n"
                         "Environment: PORT (8080), HOST (127.0.0.1), WEB_ROOT (web),\n"
                         "DOWNLOAD_ROOT (web-downloads), YTPLAYLIST_ENGINE (yt-dlp).\n";
            return 0;
        }
        fs::path executable = fs::absolute(fs::u8path(argv[0])).parent_path();
        fs::path web = fs::u8path(setting("WEB_ROOT", (executable / "web").u8string()));
        if (!fs::exists(web / "index.html")) web = fs::u8path(setting("WEB_ROOT", "web"));
        if (!fs::exists(web / "index.html")) throw std::runtime_error("web/index.html is missing");
        const auto root = fs::absolute(fs::u8path(setting("DOWNLOAD_ROOT", "web-downloads")));
        fs::create_directories(root);
        // Abandoned files never survive a server restart.
        for (const auto& item : fs::directory_iterator(root)) {
            const auto name = item.path().filename().string();
            if (item.is_directory() && name.size() == 32 &&
                name.find_first_not_of("0123456789abcdef") == std::string::npos)
                fs::remove_all(item.path());
        }
        const auto engine = setting("YTPLAYLIST_ENGINE", "yt-dlp");
        bool available = false;
        try {
            available = run({engine, "--version"}, root / "engine-check.log", 10) == 0 &&
                run({setting("FFMPEG", "ffmpeg"), "-version"}, root / "ffmpeg-check.log", 10) == 0 &&
                run({setting("DENO", "deno"), "--version"}, root / "runtime-check.log", 10) == 0;
        } catch (const std::exception& error) { std::cerr << error.what() << '\n'; }

        std::mutex mutex;
        std::map<std::string, std::shared_ptr<Job>> jobs;
        std::map<std::string, Clock::time_point> visitors;
        bool busy = false;
        const bool proxied = setting("TRUST_PROXY", "0") == "1";
        // A static copy of the website on another origin may use this server.
        const auto partner = setting("ALLOWED_ORIGIN", "");
        httplib::Server server;
        server.set_payload_max_length(4096);
        server.set_read_timeout(15, 0);
        server.set_write_timeout(60, 0);
        server.set_default_headers({{"X-Content-Type-Options", "nosniff"},
            {"Referrer-Policy", "no-referrer"}, {"X-Frame-Options", "DENY"},
            {"Content-Security-Policy", "default-src 'self'; img-src 'self' https://i.ytimg.com; style-src 'self'; script-src 'self'; font-src 'self'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'"}});
        server.set_mount_point("/", web.u8string());
        server.set_post_routing_handler([&](const httplib::Request& req, httplib::Response& res) {
            if (!partner.empty() && req.get_header_value("Origin") == partner) {
                res.set_header("Access-Control-Allow-Origin", partner);
                res.set_header("Vary", "Origin");
            }
        });
        server.Options(R"(/api/.*)", [](const httplib::Request&, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Methods", "GET, POST");
            res.set_header("Access-Control-Allow-Headers", "Content-Type");
            res.set_header("Access-Control-Max-Age", "600");
            res.status = 204;
        });
        server.Get("/api/health", [&](const auto&, auto& res) {
            reply(res, 200, {{"ready", available}, {"max_items", 25}, {"retention_minutes", 60}});
        });
        server.Post("/api/jobs", [&](const httplib::Request& req, httplib::Response& res) {
            const auto origin = req.get_header_value("Origin");
            const auto host = req.get_header_value("Host");
            const bool trusted = !partner.empty() && origin == partner;
            if (!trusted && ((!origin.empty() && origin != "https://" + host && origin != "http://" + host) ||
                req.get_header_value("Sec-Fetch-Site") == "cross-site")) {
                reply(res, 403, {{"error", "Open the downloader website to start a download."}}); return;
            }
            if (!available) {
                reply(res, 503, {{"error", "Downloads are unavailable. Please try again later."}}); return;
            }
            auto body = json::parse(req.body, nullptr, false);
            if (!body.is_object() || !body.contains("url") || !body["url"].is_string() ||
                !body.contains("mode") || !body["mode"].is_string()) {
                reply(res, 400, {{"error", "Enter a YouTube link and choose video or audio."}}); return;
            }
            const auto url = body["url"].get<std::string>();
            const auto mode = body["mode"].get<std::string>();
            if (url.size() > 2048 || !youtube_url(url) || (mode != "video" && mode != "audio")) {
                reply(res, 400, {{"error", "Paste a valid YouTube playlist or video link."}}); return;
            }
            std::lock_guard<std::mutex> lock(mutex);
            for (auto it = jobs.begin(); it != jobs.end();) {
                if (finished(*it->second) &&
                    Clock::now() - it->second->created > std::chrono::hours(1)) {
                    fs::remove_all(it->second->folder); it = jobs.erase(it);
                } else ++it;
            }
            for (auto it = visitors.begin(); it != visitors.end();) {
                if (Clock::now() - it->second > std::chrono::minutes(1)) it = visitors.erase(it);
                else ++it;
            }
            if (busy) { reply(res, 429, {{"error", "The downloader is busy. Try again in a moment."}}); return; }
            const auto visitor = client(req, proxied);
            if (visitors.count(visitor)) {
                reply(res, 429, {{"error", "Please wait a minute before starting another download."}}); return;
            }
            if (jobs.size() >= 100 || fs::space(root).available < 4ull * 1024 * 1024 * 1024) {
                reply(res, 503, {{"error", "Download storage is full. Please try again later."}}); return;
            }
            auto job = std::make_shared<Job>();
            job->id = identifier(); job->folder = root / job->id;
            fs::create_directory(job->folder);
            job->visitor = visitor;
            jobs[job->id] = job;
            visitors[visitor] = Clock::now(); busy = true;
            std::thread([&, job, url, mode] {
                try {
                    // The playlist is listed first so the page can show every item while it downloads.
                    try {
                        run({engine, "--ignore-config", "--no-plugin-dirs", "--flat-playlist", "--yes-playlist",
                            "--skip-download", "--playlist-end", "25", "--socket-timeout", "20", "--print-to-file",
                            "%(.{id,title,duration,playlist_title})j", (job->folder / "items.jsonl").u8string(),
                            "--", url}, job->folder / "list.log", 60, &job->cancel);
                    } catch (const std::exception&) {}
                    json items = json::array();
                    std::string title;
                    std::ifstream list(job->folder / "items.jsonl", std::ios::binary);
                    for (std::string line; std::getline(list, line);) {
                        const auto item = json::parse(line, nullptr, false);
                        if (!item.is_object()) continue;
                        auto text = [&](const char* key) {
                            return item.contains(key) && item[key].is_string() ?
                                item[key].get<std::string>() : std::string();
                        };
                        const auto id = text("id");
                        if (id.empty() || id.size() > 20 || id.find_first_not_of(
                            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos) continue;
                        if (title.empty()) title = text("playlist_title");
                        items.push_back({{"id", id}, {"title", text("title")},
                            {"duration", item.contains("duration") && item["duration"].is_number() ? item["duration"] : json()}});
                    }
                    list.close();
                    {
                        std::lock_guard<std::mutex> guard(mutex);
                        job->items = items; job->title = title; job->state = "downloading";
                    }
                    std::vector<std::string> args{engine, "--ignore-config", "--no-plugin-dirs",
                        "--yes-playlist", "--no-abort-on-error", "--newline", "--no-colors",
                        "--restrict-filenames", "--concurrent-fragments", "8", "--socket-timeout", "20",
                        "--retries", "3", "--fragment-retries", "3", "--playlist-end", "25",
                        "--max-filesize", "75M", "--paths", job->folder.u8string(),
                        "--output", "%(playlist_index)03d - %(title)s [%(id)s].%(ext)s"};
                    if (mode == "audio") args.insert(args.end(), {"--format", "bestaudio/best",
                        "--extract-audio", "--audio-format", "mp3"});
                    else args.insert(args.end(), {"--format", "bv*[height<=1080]+ba/b[height<=1080]/b",
                        "--format-sort", "vcodec:h264,res:1080,acodec:m4a", "--merge-output-format", "mp4"});
                    args.insert(args.end(), {"--", url});
                    const int code = run(args, job->folder / "progress.log", 1800, &job->cancel);
                    if (job->cancel) throw std::runtime_error("Download cancelled.");
                    std::vector<fs::path> files;
                    uint64_t total = 0;
                    std::vector<fs::path> found;
                    for (const auto& file : fs::directory_iterator(job->folder))
                        if (file.is_regular_file()) found.push_back(file.path());
                    for (auto path : found) {
                        const auto ext = path.extension().string();
                        if (ext != ".mp3" && ext != ".mp4" && ext != ".webm" && ext != ".mkv" &&
                            ext != ".m4a" && ext != ".opus") continue;
                        // A single video has no playlist position.
                        const auto name = path.filename().u8string();
                        if (name.rfind("NA - ", 0) == 0) {
                            const auto renamed = job->folder / fs::u8path(name.substr(5));
                            fs::rename(path, renamed); path = renamed;
                        }
                        total += fs::file_size(path); files.push_back(path);
                    }
                    std::sort(files.begin(), files.end());
                    if (files.empty()) throw std::runtime_error(code == 124 ?
                        "Download timed out. Try a smaller playlist." :
                        "No files could be downloaded. Check that the playlist is public and try again.");
                    if (total > 2ull * 1024 * 1024 * 1024) throw std::runtime_error("Playlist exceeds the 2 GB limit.");
                    { std::lock_guard<std::mutex> guard(mutex); job->state = "packing"; }
                    make_zip(job->folder / "playlist.zip", files);
                    std::lock_guard<std::mutex> guard(mutex);
                    job->files = files; job->code = code;
                    job->state = code == 0 ? "complete" : "partial";
                    job->message = code == 0 ? "Your files are ready." : "Some items were unavailable. The downloaded files are ready.";
                } catch (const std::exception& error) {
                    std::lock_guard<std::mutex> guard(mutex);
                    job->state = job->cancel ? "cancelled" : "failed"; job->message = error.what();
                    if (job->cancel) {
                        std::error_code ignored;
                        fs::remove_all(job->folder, ignored);
                        visitors.erase(job->visitor);
                    }
                }
                std::lock_guard<std::mutex> guard(mutex); busy = false;
            }).detach();
            reply(res, 202, {{"id", job->id}, {"state", job->state}});
        });
        server.Get(R"(/api/jobs/([a-f0-9]{32}))", [&](const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(mutex);
            auto found = jobs.find(req.matches[1].str());
            if (found == jobs.end()) { reply(res, 404, {{"error", "This download has expired. Start a new one."}}); return; }
            const auto& job = found->second;
            if (Clock::now() - job->created > std::chrono::hours(1)) {
                reply(res, 410, {{"error", "This download has expired. Start a new one."}}); return;
            }
            json files = json::array();
            for (size_t i = 0; i < job->files.size(); ++i) files.push_back({
                {"name", job->files[i].filename().u8string()}, {"bytes", fs::file_size(job->files[i])},
                {"url", "/api/jobs/" + job->id + "/files/" + std::to_string(i)}});
            reply(res, 200, {{"id", job->id}, {"state", job->state}, {"message", job->message},
                {"title", job->title}, {"items", item_states(*job)},
                {"log", read_tail(job->folder / "progress.log")}, {"files", files},
                {"archive", files.empty() ? "" : "/api/jobs/" + job->id + "/archive"}});
        });
        server.Post(R"(/api/jobs/([a-f0-9]{32})/cancel)", [&](const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(mutex);
            auto found = jobs.find(req.matches[1].str());
            if (found == jobs.end()) { reply(res, 404, {{"error", "This download has expired. Start a new one."}}); return; }
            if (finished(*found->second)) { reply(res, 409, {{"error", "This download has already finished."}}); return; }
            found->second->cancel = true;
            reply(res, 202, {{"id", found->second->id}, {"state", "cancelling"}});
        });
        auto send_file = [&](const httplib::Request& req, httplib::Response& res, bool archive) {
            fs::path file;
            {
                std::lock_guard<std::mutex> lock(mutex);
                auto found = jobs.find(req.matches[1].str());
                if (found == jobs.end()) { reply(res, 404, {{"error", "Download expired."}}); return; }
                const auto& job = found->second;
                if (Clock::now() - job->created > std::chrono::hours(1)) {
                    reply(res, 410, {{"error", "Download expired."}}); return;
                }
                if (job->state != "complete" && job->state != "partial") {
                    reply(res, 409, {{"error", "Your files are still being prepared."}}); return;
                }
                if (archive) file = job->folder / "playlist.zip";
                else {
                    const auto index = std::stoul(req.matches[2].str());
                    if (index >= job->files.size()) { reply(res, 404, {{"error", "File not found."}}); return; }
                    file = job->files[index];
                }
            }
            res.set_header("Content-Disposition", "attachment; filename=\"" + file.filename().string() + "\"");
            res.set_header("Cache-Control", "private, no-store");
            res.set_file_content(file.u8string(), archive ? "application/zip" : "application/octet-stream");
        };
        server.Get(R"(/api/jobs/([a-f0-9]{32})/archive)", [&](const auto& req, auto& res) { send_file(req, res, true); });
        server.Get(R"(/api/jobs/([a-f0-9]{32})/files/([0-9]{1,3}))", [&](const auto& req, auto& res) { send_file(req, res, false); });
        server.set_exception_handler([](const auto&, auto& res, std::exception_ptr) {
            reply(res, 500, {{"error", "Something went wrong. Please try again."}});
        });
        std::thread([&] {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::minutes(1));
                std::lock_guard<std::mutex> lock(mutex);
                for (auto it = jobs.begin(); it != jobs.end();) {
                    if (finished(*it->second) &&
                        Clock::now() - it->second->created > std::chrono::hours(1)) {
                        std::error_code error;
                        fs::remove_all(it->second->folder, error);
                        it = jobs.erase(it);
                    } else ++it;
                }
            }
        }).detach();
        const auto host = setting("HOST", "127.0.0.1");
        const auto port = std::stoi(setting("PORT", "8080"));
        std::cout << "Open http://" << host << ':' << port << "\n" << std::flush;
        if (!server.listen(host, port)) throw std::runtime_error("Could not listen on the requested port");
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
