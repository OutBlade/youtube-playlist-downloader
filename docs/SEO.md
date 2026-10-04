# Search visibility

Published pages:

- Tool: https://outblade.github.io/youtube-playlist-downloader/
- Download guide: https://outblade.github.io/youtube-playlist-downloader/guide/
- Sitemap: https://outblade.github.io/youtube-playlist-downloader/sitemap.xml

Both pages have distinct search titles and descriptions, absolute canonical URLs,
Open Graph and Twitter share metadata, and a branded 1200 x 630 JPEG preview.
The app has WebApplication structured data; the guide has TechArticle data.
No ratings, reviews, or usage counts are invented. Structured data describes the
product but does not imply eligibility for Google's app review rich results.

The homepage's guide and FAQ are ordinary HTML, readable without JavaScript.
Internal links connect the two pages, the downloader, and its source code.
Fonts are self-hosted and the display font is preloaded. Preserve the minimal
design and avoid duplicate landing pages made only to target keyword variants.

Run `python tests/check_seo.py` before publication. The Pages workflow also checks
metadata, JSON-LD, headings, local links, sitemap URLs, and share-image dimensions.

The backend sends `X-Robots-Tag: noindex, nofollow` for API responses and downloads.
This is an indexing instruction, not access control; job identifiers remain
private links. The canonical page points to the stable GitHub Pages address.

## Search Console setup

1. In Google Search Console, add the URL-prefix property
   `https://outblade.github.io/youtube-playlist-downloader/`.
2. Choose HTML-file verification and put Google's supplied file in `web/`.
   Publish it, then complete verification. Do not invent the verification token.
3. Submit `sitemap.xml` and inspect the homepage and guide URLs.
4. Request indexing if available. Check impressions, queries, clicks, and indexing
   status after search engines have had time to crawl the pages.

GitHub Pages serves this project under a path. Crawlers use robots.txt only at
`https://outblade.github.io/robots.txt`, not inside the project directory. A
project-level robots.txt would be ineffective, so none is added. The sitemap is
linked in the pages and can be submitted directly in Search Console. Changes to
the host-root robots file belong to the owner's root-site repository.

## Claims and promotion

Describe real features: MP3 with cover art, video up to 1080p, ZIP64 archives,
known unavailable-entry skipping, C++17 backend, yt-dlp extraction, and self-hosting.
Do not promise unlimited hardware capacity, guaranteed speed, 24/7 hosting,
private-video access, top search rankings, or a specific traffic increase.

References:

- https://developers.google.com/search/docs/fundamentals/seo-starter-guide
- https://developers.google.com/search/docs/crawling-indexing/robots/intro
- https://developers.google.com/search/docs/appearance/structured-data/software-app
