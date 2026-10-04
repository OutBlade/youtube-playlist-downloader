"""Exercise the real yt-dlp manifest dispatcher without network or downloads.

Run in the runtime container, optionally passing a folder of server worker manifests.
"""
import json
from pathlib import Path
import sys
import tempfile
import yt_dlp


class Engine(yt_dlp.YoutubeDL):
    def extract_info(self, url, **kwargs):
        assert kwargs.get('ie_key') == 'Youtube'
        return {'id': url.split('v=')[-1], 'title': 'Fresh extracted title', '_type': 'video'}

    def process_video_result(self, info, download=True):
        self.seen.append(info)
        return info


with tempfile.TemporaryDirectory() as folder:
    fixture = Path(folder) / 'worker.json'
    fixture.write_text(json.dumps([{'_type': 'url_transparent', 'ie_key': 'Youtube',
                                   'url': 'https://www.youtube.com/watch?v=abc',
                                   'playlist_index': 30, 'playlist_title': 'Album', 'playlist': 'Album'}]))
    paths = list(Path(sys.argv[1]).glob('worker-*.json')) if len(sys.argv) > 1 else [fixture]
    assert paths
    for path in paths:
        expected = json.loads(path.read_text())
        with Engine({'quiet': True, 'ignoreerrors': True}) as engine:
            engine.seen = []
            assert engine.download_with_info_file(str(path)) == 0
            assert len(engine.seen) == len(expected)
            for info, entry in zip(engine.seen, expected):
                assert info['playlist_index'] == entry['playlist_index']
                assert info['playlist_title'] == entry['playlist_title']
                assert info['title'] == 'Fresh extracted title'
    print('Real yt-dlp: worker manifests preserve original track indices and album metadata.')
