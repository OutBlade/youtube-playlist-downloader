# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

## Stack

Confirmed: plain HTML, CSS, and JavaScript with a C++ backend. Existing C++17 CLI uses yt-dlp for extraction and downloading.

## Users

Public visitors who paste a YouTube playlist URL and download video or audio through their browser.

## Product Purpose

Make the existing simple playlist downloader usable as a website, without requiring visitors to install the CLI.

## Capabilities and Constraints

Video and MP3 downloads, concurrent fragments, playlist ordering, progress and file delivery. A public server must run the C++ backend, yt-dlp, ffmpeg, and a supported JavaScript runtime. GitHub Pages serves the page at a fixed address; downloads run on the owner's computer in Docker, reached through a Cloudflare quick tunnel whose address `start-public.ps1` publishes to the `backend` branch. Existing CLI remains available.

## Brand Commitments

Minimal, clean design fitting the user's supplied BLADE wordmark: `C:/Users/Semi/Desktop/2.Semester/Karriere/Blade_Logo_v2.gif`. Preserve the actual artwork. User chose building directly in code.

## Evidence on Hand

Published OutBlade/youtube-playlist-downloader repository, v1.0.0 binaries, and passing cross-platform CLI tests. Live playlist, single-video and MP3 downloads verified through the public address. No measured speed claims.

## Product Principles

- The primary task should be immediately visible.
- Show real download state and actionable errors.
- Match the supplied brand with restrained presentation.
- Keep browser controls simple and backend configuration outside the visitor flow.
