"""Check the published static pages, structured data, sitemap, and local links."""
from html.parser import HTMLParser
import json
from pathlib import Path
import struct
from urllib.parse import urlparse, unquote
import xml.etree.ElementTree as ET

WEB = Path(__file__).resolve().parents[1] / 'web'
BASE = 'https://outblade.github.io/youtube-playlist-downloader/'


class Page(HTMLParser):
    def __init__(self):
        super().__init__()
        self.meta, self.links, self.schema = {}, [], []
        self.canonical, self.h1, self.title, self.capture = '', 0, '', ''
        self.buffer = ''

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == 'meta':
            self.meta[attrs.get('name', attrs.get('property'))] = attrs.get('content', '')
        if tag == 'link' and attrs.get('rel') == 'canonical':
            self.canonical = attrs['href']
        if tag == 'h1':
            self.h1 += 1
        if tag in ['a', 'link'] and 'href' in attrs:
            self.links.append(attrs['href'])
        if tag in ['img', 'script'] and 'src' in attrs:
            self.links.append(attrs['src'])
        if tag == 'title' or tag == 'script' and attrs.get('type') == 'application/ld+json':
            self.capture = tag
            self.buffer = ''

    def handle_data(self, data):
        if self.capture:
            self.buffer += data

    def handle_endtag(self, tag):
        if tag != self.capture:
            return
        if tag == 'title':
            self.title = self.buffer
        else:
            self.schema.append(json.loads(self.buffer))
        self.capture = ''


seen = set()
for relative in ['index.html', 'guide/index.html']:
    path = WEB / relative
    page = Page()
    page.feed(path.read_text(encoding='utf-8'))
    canonical = BASE + ('guide/' if relative.startswith('guide/') else '')
    assert page.canonical == canonical
    assert page.meta['og:url'] == canonical
    assert page.h1 == 1 and 20 < len(page.title) < 75
    assert page.title not in seen
    seen.add(page.title)
    assert 80 < len(page.meta['description']) < 180
    assert page.meta['robots'] == 'index,follow,max-image-preview:large'
    assert page.meta['twitter:card'] == 'summary_large_image'
    assert page.schema
    for schema in page.schema:
        assert schema['@context'] == 'https://schema.org'
        assert 'aggregateRating' not in schema
    for link in page.links:
        parsed = urlparse(link)
        if parsed.scheme or parsed.netloc or not parsed.path:
            continue
        target = (path.parent / unquote(parsed.path)).resolve()
        assert target.is_relative_to(WEB.resolve()) and target.exists(), (relative, link)
    print(relative, 'metadata, structured data, heading and local links verified')

urls = {loc.text for loc in ET.parse(WEB / 'sitemap.xml').findall('.//{*}loc')}
assert urls == {BASE, BASE + 'guide/'}
image = (WEB / 'assets/social-preview.jpg').read_bytes()
assert image[:2] == b'\xff\xd8'
offset = 2
dimensions = None
while offset < len(image):
    assert image[offset] == 255
    marker = image[offset + 1]
    length = struct.unpack('>H', image[offset + 2:offset + 4])[0]
    if marker in [0xc0, 0xc1, 0xc2]:
        height, width = struct.unpack('>HH', image[offset + 5:offset + 9])
        dimensions = width, height
        break
    offset += 2 + length
assert dimensions == (1200, 630)
print('Sitemap and 1200 x 630 social image verified')
