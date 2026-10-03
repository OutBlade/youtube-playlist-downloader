# YouTube Playlist Downloader

A tiny C++17 command-line frontend for [yt-dlp](https://github.com/yt-dlp/yt-dlp).
Paste a playlist link and download. No GUI, accounts, or database.

- Eight concurrent fragments by default; change with `-j 1..32`.
- Resume interrupted downloads and skip completed videos using `downloaded.txt`.
- Numbered files grouped by playlist title.
- Video or MP3 audio, with live yt-dlp progress.
- Windows, Linux, and macOS. Arguments are passed directly to a process, without a shell.

## Get started

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
