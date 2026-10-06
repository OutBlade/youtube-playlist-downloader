# YouTube Playlist Downloader

**[Open the downloader](https://outblade.github.io/youtube-playlist-downloader/)**

BLADE is a minimal browser interface with a C++ HTTP backend. Paste a playlist,
choose video or MP3, watch every item arrive with its thumbnail, and save the ZIP.

Free and open source, with no signup and no application playlist-size cap.
Known private/deleted entries are skipped before download workers start.
See the [MP3 and video download guide](https://outblade.github.io/youtube-playlist-downloader/guide/)
for formats, ZIP archives, and troubleshooting. Playlists enter a shared FIFO
queue. The public downloader currently runs on the owner's computer; the
production Docker/Caddy setup below is ready for migration to an always-on host.

## Website

The page is published on GitHub Pages and stays at one address. Pages cannot
run downloads, so the page talks to a separate C++ server. The current public
instance uses a Cloudflare quick tunnel to the owner's computer; the permanent
server recipe uses Docker, Caddy HTTPS, and persistent volumes.

```powershell
./start-public.ps1 -Watch
```

The script starts Docker if needed, starts the server and its tunnel, and
publishes the tunnel's current HTTPS address to the `backend` branch, where the
page looks it up. The quick-tunnel address changes whenever the tunnel restarts;
`-Watch` republishes it within a minute. While the computer is off, the page
says the downloader is offline and reconnects on its own.

To run it only for yourself:

```sh
docker compose up --build -d
```

Open `http://localhost:8080`. The container installs yt-dlp, ffmpeg, and Deno.
Downloads leave from your own connection, which YouTube treats better than
most hosting providers' addresses. For a permanent server address, put your
own HTTPS reverse proxy in front of `127.0.0.1:8080` and preserve its original
Host header. `TRUST_PROXY=1` makes the per-visitor limit use the address
reported by the proxy. `ALLOWED_ORIGIN` names the one other website origin
that may use the server; both are set in `compose.yaml`.

To prepare an always-on Linux host, point a DNS A record at its fixed public IP,
allow inbound TCP ports 80 and 443 (plus UDP 443 for HTTP/3), copy
`.env.production.example` to `.env`, and set `API_DOMAIN` to the DNS name. Then
run:

```sh
docker compose -f compose.production.yaml up --build -d
```

Caddy obtains and renews the HTTPS certificate automatically. A named volume
preserves its certificate data across container rebuilds, and Docker restarts
the server after a host reboot. The server still needs a host
that is kept running, adequate disk space, and a network route YouTube accepts.
After deployment, publish `{"url":"https://YOUR_API_DOMAIN"}` as
`backend.json` on the repository's `backend` branch so the GitHub Pages site
connects to the permanent API. Tune the production CPU, memory, worker, and
queue settings for the selected host. The backend runs up to
`BLADE_MAX_ACTIVE_JOBS` playlists (2 by default) from a FIFO queue, with a
shared cap of `BLADE_WORKERS` downloader processes (16 by default). This lets
visitors download at the same time without multiplying the server's worker
budget. It accepts up to `BLADE_QUEUE_LIMIT` jobs (100 by default).
Queue state and completed files are held in the server container, so active and
waiting jobs restart if the server process is restarted; users can submit them
again afterward. Completed files expire after one hour and are removed on restart.

### Free cloud server hosting: Render (no credit card required)

The fastest and easiest way to host the backend for free **without entering a credit card** is **[Render](https://render.com)**:
- **100% Free**: No payment card, credit card, or billing verification required.
- **Automatic HTTPS**: Provides a free permanent URL (e.g. `https://your-app.onrender.com`) with instant SSL.
- **Docker-native**: Runs the container with yt-dlp, ffmpeg, Deno, and the C++ engine.
- **24/7 Keepalive**: The repository includes `.github/workflows/keepalive.yml` which automatically pings your server every 14 minutes so it stays awake 24/7 without sleeping.

#### 1-Click Deploy

[![Deploy to Render](https://render.com/images/deploy-to-render-button.svg)](https://render.com/deploy?repo=https://github.com/OutBlade/youtube-playlist-downloader)

1. Sign up for free at [render.com](https://render.com) using your GitHub account (no card needed).
2. Click the button above (or in Render click **New +** → **Blueprint** → connect `OutBlade/youtube-playlist-downloader`).
3. Render reads `render.yaml` and deploys your service. When ready, copy your service's URL (e.g. `https://youtube-playlist-downloader-xxxx.onrender.com`).
4. **Connect the website to your new server:**
   - **From GitHub (easiest):** Go to your repository's **Actions** tab → select **Set Backend URL** → click **Run workflow** and paste your Render URL.
   - **Or from your terminal:**
     ```powershell
     .\deploy\publish-backend.ps1 -Address https://YOUR_APP.onrender.com
     ```

Once published, [https://outblade.github.io/youtube-playlist-downloader/](https://outblade.github.io/youtube-playlist-downloader/) will immediately route all downloads to your free cloud server!

---

### Alternative: Oracle Cloud Always Free (requires credit card)

For users who have an Oracle Cloud account (which requires a payment card for identity verification), Oracle's Always Free Ampere A1 VM offers up to 4 ARM OCPUs and 24 GB RAM:
- Auto-tuned concurrency: `deploy/oracle-free-bootstrap.sh` detects provisioned cores and RAM.
- Reclamation protection: installs `blade-keepalive.service` to prevent 7-day idle reclamation.
- SSH to the Ubuntu A1 instance and run:
  ```sh
  curl -fsSL https://raw.githubusercontent.com/OutBlade/youtube-playlist-downloader/main/deploy/oracle-free-bootstrap.sh -o /tmp/blade-bootstrap.sh
  sudo bash /tmp/blade-bootstrap.sh
  ```

To run without Docker, install yt-dlp, ffmpeg and Deno, then launch
`ytplaylist-web` (`ytplaylist-web.exe` on Windows) beside its `web/` folder.
By default it listens on `127.0.0.1:8080`. Set `HOST=0.0.0.0` to expose it to a
hosting platform. The server honors `PORT`, `WEB_ROOT`, `DOWNLOAD_ROOT`,
`YTPLAYLIST_ENGINE`, `FFMPEG`, `DENO`, `TRUST_PROXY`, and `ALLOWED_ORIGIN`
environment variables.

The playlist is fetched once and divided into worker queues, balancing known
video durations so long videos start early. `WORKERS` sets the shared maximum
number of yt-dlp processes for all active playlists (16 by default, at most 32).
`MAX_ACTIVE_JOBS` sets how many playlists may download at once (2 by default,
at most 16). `QUEUE_LIMIT` sets the maximum number of waiting plus active jobs
(100 by default, at most 1000). `FRAGMENTS` controls concurrent
fragments per video (8 by default, at most 32); it applies to fragmented streams.
Known private/deleted placeholders are skipped before extraction. Newly unavailable
videos skip on the first permanent extraction error, without extractor retries.
Transient transfer errors get one retry and sockets time out after 10 seconds.
ZIP64 packing copies each file and computes its checksum in a single disk pass.
More workers can increase throttling or CPU pressure; tune these settings for the
host's connection and memory rather than assuming a fixed maximum speed.
MP3 files carry the title, uploader, playlist name as album, track number and
the standard 480 x 360 YouTube thumbnail as cover art, without probing larger
thumbnail variants that may return 404.

The public server processes playlists from a FIFO queue without an
application-imposed item-count, file-size, ZIP-size, or total download-duration cap. ZIP64 archives
support large files and playlists with more than 65,535 entries. Video remains
1080p MP4 (H.264 where available). Actual capacity depends on server storage,
memory, network access, and YouTube availability. The server requires at least
4 GiB free before accepting a job. One download start
per client IP per minute is allowed. Files expire one hour after the job finishes
and are removed on server restart. In-progress work itself does not survive a
server restart.
Job URLs contain random identifiers: treat them as private download links.
Only recognized media files are exposed, and starts from any other origin
than `ALLOWED_ORIGIN` are rejected.

Rebuild the Docker image regularly to update YouTube extraction. Some hosting
providers' IP addresses may be blocked by YouTube; verify a real playlist from
the chosen server before opening it to visitors.

## Command-line tool

A tiny C++17 command-line frontend for [yt-dlp](https://github.com/yt-dlp/yt-dlp).
Paste a playlist link and download. No GUI, accounts, or database.

- Eight concurrent fragments by default; change with `-j 1..32`.
- Resume interrupted downloads and skip completed videos using `downloaded.txt`.
- Numbered files grouped by playlist title.
- Video or MP3 audio, with live yt-dlp progress.
- Windows, Linux, and macOS. Arguments are passed directly to a process, without a shell.

## Get started

Download a prebuilt executable from [Releases](https://github.com/OutBlade/youtube-playlist-downloader/releases/latest)
or build from source below. Windows binaries are x64; they also run on Windows ARM64
through Windows' x64 emulation. For Linux and macOS, run `chmod +x ytplaylist` after extraction.

Install **yt-dlp** and **ffmpeg** and put them on your PATH.
Follow the [official yt-dlp installation instructions](https://github.com/yt-dlp/yt-dlp#installation),
including its recommended JavaScript runtime for full YouTube support.
ffmpeg is needed to merge the highest-quality video/audio streams and to create MP3s.
You can also point to a yt-dlp executable with `--yt-dlp "path/to/yt-dlp"`.

```sh
ytplaylist "https://www.youtube.com/playlist?list=YOUR_PLAYLIST_ID"
ytplaylist --audio -o music "https://www.youtube.com/playlist?list=YOUR_PLAYLIST_ID"
ytplaylist -j 16 -o videos "https://www.youtube.com/playlist?list=YOUR_PLAYLIST_ID"
```

In PowerShell, use `./ytplaylist.exe` in place of `ytplaylist`.
Always quote URLs because playlist links can contain `&`.
Output goes into `downloads/` unless you choose a folder with `-o`.

## Build

Requires a C++17 compiler and CMake 3.16+. Python 3 runs the tests.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The executable is `build/ytplaylist` (Linux/macOS), or
`build/Release/ytplaylist.exe` with Visual Studio on Windows.
Use `-DBUILD_TESTING=OFF` to build without Python or the test helper.

## Details

The frontend is written in C++; YouTube extraction and media downloading are handled
by the separately installed yt-dlp. Update yt-dlp if YouTube changes.
`-j` parallelizes fragments within a video, rather than downloading multiple videos
at once. Speed depends on your connection, YouTube, and the selected media format;
increasing concurrency is not a guaranteed speedup.

Rerun the same command and output folder after interruption. The archive records
successful downloads only. Video and audio modes share the archive: use separate
output folders if you want both versions of the same playlist. To redownload items,
remove their archive entries and corresponding media files. Unavailable items are
reported by yt-dlp, and the program preserves its exit code.

Tests cover argument validation, Unicode output paths, process argument boundaries,
audio options, missing dependencies, and failure propagation. They use a fake
downloader and do not verify live YouTube availability.

Only download content you own or have permission to download.
