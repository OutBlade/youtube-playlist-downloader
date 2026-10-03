FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ cmake make ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /source
COPY CMakeLists.txt ./
COPY src ./src
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF && cmake --build build -j2

FROM denoland/deno:bin AS deno
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends python3 python3-venv ffmpeg ca-certificates libstdc++6 && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /opt/yt-dlp && /opt/yt-dlp/bin/pip install --no-cache-dir 'yt-dlp[default]'
COPY --from=deno /deno /usr/local/bin/deno
COPY --from=build /source/build/ytplaylist-web /app/ytplaylist-web
COPY --from=build /source/build/ytplaylist /app/ytplaylist
COPY web /app/web
RUN useradd --create-home --uid 10001 downloader && mkdir /app/downloads && chown downloader:downloader /app/downloads
WORKDIR /app
ENV PATH="/opt/yt-dlp/bin:${PATH}" HOST=0.0.0.0 PORT=8080 WEB_ROOT=/app/web DOWNLOAD_ROOT=/app/downloads DENO_NO_UPDATE_CHECK=1
USER downloader
EXPOSE 8080
HEALTHCHECK --interval=30s --timeout=10s CMD python3 -c "import json,urllib.request; assert json.load(urllib.request.urlopen('http://127.0.0.1:8080/api/health'))['ready']"
CMD ["/app/ytplaylist-web"]
