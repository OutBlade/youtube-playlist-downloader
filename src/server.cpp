#define YTPLAYLIST_NO_MAIN
#include "main.cpp"
#include "zip.hpp"
#include "httplib.h"
#include "json.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cctype>
#include <deque>
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
    Clock::time_point completed;
    int code = -1;
};
class WorkerBudget {
public:
    explicit WorkerBudget(size_t limit) : available_(limit) {}
    bool acquire(const std::atomic<bool>& cancel) {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [&] { return available_ > 0 || cancel.load(); });
        if (cancel) return false;
        --available_;
        return true;
    }
    void release() {
        { std::lock_guard<std::mutex> lock(mutex_); ++available_; }
        ready_.notify_one();
    }
private:
    std::mutex mutex_;
    std::condition_variable ready_;
    size_t available_;
};
bool finished(const Job& job) {
    return job.state == "complete" || job.state == "partial" || job.state == "failed" ||
        job.state == "cancelled";
}
std::string setting(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value && *value ? value : fallback;
}
std::string read_tail(const fs::path& path, std::streamoff limit) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    auto size = file.tellg();
    file.seekg(size > limit ? size - limit : std::streampos(0));
    return std::string(std::istreambuf_iterator<char>(file), {});
}
// One log when the job runs as a single process, one per worker otherwise.
std::vector<fs::path> logs(const fs::path& folder) {
    std::vector<fs::path> found;
    if (fs::exists(folder / "progress.log")) found.push_back(folder / "progress.log");
    for (int worker = 0; worker < 32; ++worker) {
        const auto path = folder / ("progress-" + std::to_string(worker) + ".log");
        if (fs::exists(path)) found.push_back(path);
    }
    return found;
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
        if (items[i].value("state", "queued") != "skipped") items[i]["state"] = "queued";
        index[items[i]["id"].get<std::string>()] = i;
    }
    for (const auto& path : logs(job.folder)) {
    std::ifstream log(path, std::ios::binary);
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
                const bool unavailable = line.find("rivate") != std::string::npos ||
                    line.find("deleted") != std::string::npos || line.find("removed") != std::string::npos;
                item["state"] = unavailable ? "skipped" : "failed";
                item["note"] = line.find("confirm your age") != std::string::npos ? "Age-restricted" :
                    line.find("rivate") != std::string::npos ? "Private" : unavailable ? "Deleted" : "Unavailable";
            } else if (current != &item) {
                if (current && (*current)["state"] == "active") (*current)["state"] = "done";
                current = &item; item["state"] = "active";
            }
        } else if (current && line.rfind("[download] ", 0) == 0) {
            const auto digit = line.find_first_not_of(' ', 11);
            if (line.find("larger than max-filesize") != std::string::npos) {
                (*current)["state"] = "skipped"; (*current)["note"] = "Too large";
            } else if (line.find("Finished downloading playlist") != std::string::npos) {
                if ((*current)["state"] == "active") (*current)["state"] = "done";
            } else if (digit != std::string::npos && std::isdigit(static_cast<unsigned char>(line[digit])) &&
                line.find('%') != std::string::npos) {
                (*current)["percent"] = std::atof(line.c_str() + digit);
            }
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
        std::condition_variable available_job;
        std::map<std::string, std::shared_ptr<Job>> jobs;
        std::deque<std::string> queue;
        std::map<std::string, Clock::time_point> visitors;
        size_t active_jobs = 0;
        const bool proxied = setting("TRUST_PROXY", "0") == "1";
        // A static copy of the website on another origin may use this server.
        const auto partner = setting("ALLOWED_ORIGIN", "");
        // Playlist items are downloaded by this many yt-dlp processes at once.
        const size_t parallel = static_cast<size_t>(std::clamp(std::stoi(setting("WORKERS", "16")), 1, 32));
        WorkerBudget worker_budget(parallel);
        const size_t max_active_jobs = static_cast<size_t>(std::clamp(std::stoi(setting("MAX_ACTIVE_JOBS", "2")), 1, 16));
        const auto fragments = std::to_string(std::clamp(std::stoi(setting("FRAGMENTS", "8")), 1, 32));
        httplib::Server server;
        server.set_payload_max_length(4096);
        server.set_read_timeout(15, 0);
        server.set_write_timeout(60, 0);
        server.set_default_headers({{"X-Content-Type-Options", "nosniff"},
            {"Referrer-Policy", "no-referrer"}, {"X-Frame-Options", "DENY"},
            {"Content-Security-Policy", "default-src 'self'; img-src 'self' https://i.ytimg.com; style-src 'self'; script-src 'self'; font-src 'self'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'"}});
        server.set_mount_point("/", web.u8string());
        server.set_post_routing_handler([&](const httplib::Request& req, httplib::Response& res) {
            if (req.path.rfind("/api/", 0) == 0) res.set_header("X-Robots-Tag", "noindex, nofollow");
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
            reply(res, 200, {{"ready", available}, {"max_items", nullptr}, {"retention_minutes", 60}});
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
                    Clock::now() - it->second->completed > std::chrono::hours(1)) {
                    fs::remove_all(it->second->folder); it = jobs.erase(it);
                } else ++it;
            }
            for (auto it = visitors.begin(); it != visitors.end();) {
                if (Clock::now() - it->second > std::chrono::minutes(1)) it = visitors.erase(it);
                else ++it;
            }
            const auto visitor = client(req, proxied);
            if (visitors.count(visitor)) {
                reply(res, 429, {{"error", "Please wait a minute before starting another download."}}); return;
            }
            size_t pending = 0;
            for (const auto& entry : jobs) if (!finished(*entry.second)) ++pending;
            const auto queue_limit = static_cast<size_t>(std::clamp(std::stoi(setting("QUEUE_LIMIT", "100")), 1, 1000));
            if (pending >= queue_limit) {
                reply(res, 503, {{"error", "The download queue is full. Please try again later."}}); return;
            }
            if (jobs.size() >= 1000 || fs::space(root).available < 4ull * 1024 * 1024 * 1024) {
                reply(res, 503, {{"error", "Download storage is full. Please try again later."}}); return;
            }
            auto job = std::make_shared<Job>();
            job->id = identifier(); job->folder = root / job->id; job->state = "queued";
            fs::create_directory(job->folder);
            job->visitor = visitor;
            jobs[job->id] = job;
            visitors[visitor] = Clock::now();
            queue.push_back(job->id);
            std::thread([&, job, url, mode] {
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    available_job.wait(lock, [&] {
                        return job->cancel || (active_jobs < max_active_jobs && !queue.empty() && queue.front() == job->id);
                    });
                    if (job->cancel) {
                        queue.erase(std::remove(queue.begin(), queue.end(), job->id), queue.end());
                        job->completed = Clock::now(); job->state = "cancelled";
                        job->message = "Download cancelled."; visitors.erase(job->visitor);
                        available_job.notify_all();
                        return;
                    }
                    queue.pop_front();
                    ++active_jobs;
                    job->state = "reading";
                    available_job.notify_all();
                }
                try {
                    // The playlist is listed first so the page can show every item while it downloads.
                    try {
                        run({engine, "--ignore-config", "--no-plugin-dirs", "--flat-playlist", "--yes-playlist",
                            "--skip-download", "--ignore-errors", "--socket-timeout", "10",
                            "--extractor-retries", "0", "--retries", "1", "--print-to-file",
                            "%(.{id,title,duration,playlist_title,playlist_index,availability})j", (job->folder / "items.jsonl").u8string(),
                            "--", url}, job->folder / "list.log", 0, &job->cancel);
                    } catch (const std::exception&) {}
                    json items = json::array();
                    json downloads = json::array();
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
                        // YouTube exposes unavailable placeholders in its flat playlist listing.
                        // Never send these to the video extractor or consume a worker slot for them.
                        const bool private_video = text("availability") == "private" || text("title") == "[Private video]";
                        const bool deleted_video = text("title") == "[Deleted video]";
                        if (private_video || deleted_video) {
                            items.back()["state"] = "skipped";
                            items.back()["note"] = private_video ? "Private" : "Deleted";
                            continue;
                        }
                        // URL-transparent entries preserve track/album metadata while the engine
                        // retrieves fresh formats, without requesting the original playlist again.
                        json download{{"_type", "url_transparent"}, {"ie_key", "Youtube"},
                            {"url", "https://www.youtube.com/watch?v=" + id},
                            // This standard cover avoids probing speculative high-resolution variants.
                            {"thumbnails", json::array({{{"url", "https://i.ytimg.com/vi/" + id + "/hqdefault.jpg"},
                                {"id", "cover"}, {"width", 480}, {"height", 360}}})},
                            {"playlist_title", title}, {"playlist", title}};
                        // A direct video is not playlist item 1. Preserve absent metadata so
                        // yt-dlp creates a clean video filename instead of a synthetic index.
                        if (item.contains("playlist_index") && item["playlist_index"].is_number_integer())
                            download["playlist_index"] = item["playlist_index"];
                        downloads.push_back(std::move(download));
                    }
                    list.close();
                    {
                        std::lock_guard<std::mutex> guard(mutex);
                        job->items = items;
                        job->title = title.empty() && items.size() == 1 ? items[0].value("title", std::string()) : title;
                        job->state = "downloading";
                    }
                    if (job->cancel) throw std::runtime_error("Download cancelled.");
                    if (downloads.empty() && !items.empty())
                        throw std::runtime_error("All playlist videos are private or deleted.");
                    const size_t workers = std::max<size_t>(1, std::min(parallel, downloads.size()));
                    std::vector<json> batches(workers, json::array());
                    std::vector<double> loads(workers, 0);
                    std::vector<size_t> order;
                    for (size_t i = 0; i < items.size(); ++i)
                        if (items[i].value("state", "queued") != "skipped") order.push_back(i);
                    auto duration = [&](size_t i) {
                        return items[i]["duration"].is_number() ? std::max(1.0, items[i]["duration"].get<double>()) : 300.0;
                    };
                    std::vector<size_t> sorted(downloads.size());
                    for (size_t i = 0; i < sorted.size(); ++i) sorted[i] = i;
                    std::stable_sort(sorted.begin(), sorted.end(), [&](size_t a, size_t b) {
                        return duration(order[a]) > duration(order[b]);
                    });
                    for (size_t i : sorted) {
                        const auto worker = static_cast<size_t>(std::min_element(loads.begin(), loads.end()) - loads.begin());
                        batches[worker].push_back(downloads[i]);
                        loads[worker] += duration(order[i]);
                    }
                    for (size_t worker = 0; worker < workers; ++worker) {
                        std::ofstream manifest(job->folder / ("worker-" + std::to_string(worker) + ".json"));
                        manifest << batches[worker].dump(-1, ' ', false, json::error_handler_t::replace);
                        manifest.close();
                        if (!manifest) throw std::runtime_error("Could not prepare download queue.");
                    }
                    auto arguments = [&](size_t worker) {
                        std::vector<std::string> args{engine, "--ignore-config", "--no-plugin-dirs",
                            "--yes-playlist", "--ignore-errors", "--newline", "--no-colors",
                            "--restrict-filenames", "--concurrent-fragments", fragments,
                            "--socket-timeout", "10", "--extractor-retries", "0",
                            "--retries", "1", "--fragment-retries", "1", "--file-access-retries", "1",
                            "--buffer-size", "1M", "--progress-delta", "1",
                            "--paths", job->folder.u8string(),
                            "--output", "%(playlist_index)03d - %(title)s [%(id)s].%(ext)s"};
                        if (mode == "audio") args.insert(args.end(), {"--format", "bestaudio/best",
                            "--extract-audio", "--audio-format", "mp3", "--embed-metadata",
                            "--embed-thumbnail", "--convert-thumbnails", "jpg",
                            "--parse-metadata", "%(playlist_title|)s:%(meta_album)s",
                            "--parse-metadata", "%(playlist_index|)s:%(meta_track)s"});
                        else args.insert(args.end(), {"--format", "bv*[height<=1080]+ba/b[height<=1080]/b",
                            "--format-sort", "vcodec:h264,res:1080,acodec:m4a", "--merge-output-format", "mp4"});
                        if (!downloads.empty()) args.insert(args.end(), {"--load-info-json",
                            (job->folder / ("worker-" + std::to_string(worker) + ".json")).u8string()});
                        else args.insert(args.end(), {"--", url}); // Listing unavailable: let the engine resolve the URL.
                        return args;
                    };
                    std::vector<int> codes(workers, 1);
                    std::vector<std::thread> pool;
                    for (size_t worker = 0; worker < workers; ++worker) pool.emplace_back([&, worker] {
                        if (!worker_budget.acquire(job->cancel)) { codes[worker] = 125; return; }
                        try {
                            if (job->cancel) codes[worker] = 125;
                            else codes[worker] = run(arguments(worker), job->folder / (workers > 1 ?
                                "progress-" + std::to_string(worker) + ".log" : "progress.log"), 0, &job->cancel);
                        } catch (const std::exception&) {}
                        worker_budget.release();
                    });
                    for (auto& thread : pool) thread.join();
                    int code = 0;
                    for (const int result : codes) if (result == 124 || (result && !code)) code = result;
                    if (job->cancel) throw std::runtime_error("Download cancelled.");
                    std::vector<fs::path> files;
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
                        files.push_back(path);
                    }
                    std::sort(files.begin(), files.end());
                    if (files.empty()) throw std::runtime_error(code == 124 ?
                        "Download timed out. Try a smaller playlist." :
                        "No files could be downloaded. Check that the playlist is public and try again.");
                    { std::lock_guard<std::mutex> guard(mutex); job->state = "packing"; }
                    make_zip(job->folder / "playlist.zip", files);
                    std::lock_guard<std::mutex> guard(mutex);
                    job->files = files; job->code = code;
                    job->completed = Clock::now();
                    job->state = code == 0 ? "complete" : "partial";
                    job->message = code == 0 ? "Your files are ready." : "Some items were unavailable. The downloaded files are ready.";
                } catch (const std::exception& error) {
                    std::lock_guard<std::mutex> guard(mutex);
                    job->completed = Clock::now();
                    job->state = job->cancel ? "cancelled" : "failed"; job->message = error.what();
                    if (job->cancel) {
                        std::error_code ignored;
                        fs::remove_all(job->folder, ignored);
                        visitors.erase(job->visitor);
                    }
                }
                std::lock_guard<std::mutex> guard(mutex);
                --active_jobs;
                available_job.notify_all();
            }).detach();
            reply(res, 202, {{"id", job->id}, {"state", "queued"}});
        });
        server.Get(R"(/api/jobs/([a-f0-9]{32}))", [&](const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(mutex);
            auto found = jobs.find(req.matches[1].str());
            if (found == jobs.end()) { reply(res, 404, {{"error", "This download has expired. Start a new one."}}); return; }
            const auto& job = found->second;
            if (finished(*job) && Clock::now() - job->completed > std::chrono::hours(1)) {
                reply(res, 410, {{"error", "This download has expired. Start a new one."}}); return;
            }
            std::string log;
            const auto sources = logs(job->folder);
            for (const auto& path : sources) log += read_tail(path, 12000 / static_cast<std::streamoff>(sources.size()));
            json files = json::array();
            for (size_t i = 0; i < job->files.size(); ++i) files.push_back({
                {"name", job->files[i].filename().u8string()}, {"bytes", fs::file_size(job->files[i])},
                {"url", "/api/jobs/" + job->id + "/files/" + std::to_string(i)}});
            reply(res, 200, {{"id", job->id}, {"state", job->state}, {"message", job->message},
                {"title", job->title}, {"items", item_states(*job)},
                {"log", log}, {"files", files},
                {"archive", files.empty() ? "" : "/api/jobs/" + job->id + "/archive"}});
        });
        server.Post(R"(/api/jobs/([a-f0-9]{32})/cancel)", [&](const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(mutex);
            auto found = jobs.find(req.matches[1].str());
            if (found == jobs.end()) { reply(res, 404, {{"error", "This download has expired. Start a new one."}}); return; }
            if (finished(*found->second)) { reply(res, 409, {{"error", "This download has already finished."}}); return; }
            found->second->cancel = true;
            available_job.notify_all();
            reply(res, 202, {{"id", found->second->id}, {"state", "cancelling"}});
        });
        auto send_file = [&](const httplib::Request& req, httplib::Response& res, bool archive) {
            fs::path file;
            {
                std::lock_guard<std::mutex> lock(mutex);
                auto found = jobs.find(req.matches[1].str());
                if (found == jobs.end()) { reply(res, 404, {{"error", "Download expired."}}); return; }
                const auto& job = found->second;
                if (finished(*job) && Clock::now() - job->completed > std::chrono::hours(1)) {
                    reply(res, 410, {{"error", "Download expired."}}); return;
                }
                if (job->state != "complete" && job->state != "partial") {
                    reply(res, 409, {{"error", "Your files are still being prepared."}}); return;
                }
                if (archive) file = job->folder / "playlist.zip";
                else {
                    const auto index = std::stoull(req.matches[2].str());
                    if (index >= job->files.size()) { reply(res, 404, {{"error", "File not found."}}); return; }
                    file = job->files[index];
                }
            }
            res.set_header("Content-Disposition", "attachment; filename=\"" + file.filename().string() + "\"");
            res.set_header("Cache-Control", "private, no-store");
            res.set_file_content(file.u8string(), archive ? "application/zip" : "application/octet-stream");
        };
        server.Get(R"(/api/jobs/([a-f0-9]{32})/archive)", [&](const auto& req, auto& res) { send_file(req, res, true); });
        server.Get(R"(/api/jobs/([a-f0-9]{32})/files/([0-9]{1,19}))", [&](const auto& req, auto& res) { send_file(req, res, false); });
        server.set_exception_handler([](const auto&, auto& res, std::exception_ptr) {
            reply(res, 500, {{"error", "Something went wrong. Please try again."}});
        });
        std::thread([&] {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::minutes(1));
                std::lock_guard<std::mutex> lock(mutex);
                for (auto it = jobs.begin(); it != jobs.end();) {
                    if (finished(*it->second) &&
                        Clock::now() - it->second->completed > std::chrono::hours(1)) {
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
