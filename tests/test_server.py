"""Real HTTP -> C++ process -> files -> ZIP, with deterministic fake media."""
import io
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen
import zipfile

SERVER, ENGINE, WEB = (str(Path(p).resolve()) for p in sys.argv[1:4])
sys.argv = sys.argv[:1]


class Web(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]
        self.base = f'http://127.0.0.1:{port}'
        env = dict(os.environ, HOST='127.0.0.1', PORT=str(port), WEB_ROOT=WEB,
                   DOWNLOAD_ROOT=str(Path(self.temp.name) / 'downloads'),
                   YTPLAYLIST_ENGINE=ENGINE, ALLOWED_ORIGIN='https://site.example', FFMPEG=ENGINE, DENO=ENGINE,
                   YTPLAYLIST_TEST_LOG=str(Path(self.temp.name) / 'args.txt'),
                   YTPLAYLIST_TEST_MEDIA='1', YTPLAYLIST_TEST_EXIT='0')
        if self._testMethodName == 'test_more_than_25_items_are_all_downloaded':
            env['YTPLAYLIST_TEST_COUNT'] = '30'
        self.log = open(Path(self.temp.name) / 'server.log', 'w')
        self.addCleanup(self.log.close)
        self.server = subprocess.Popen([SERVER], env=env, stdout=self.log, stderr=self.log)
        self.addCleanup(self.stop)
        for _ in range(100):
            try:
                self.request('/api/health')
                break
            except OSError:
                if self.server.poll() is not None:
                    self.fail('Server exited during startup')
                time.sleep(.1)
        else:
            self.fail('Server did not become ready')

    def stop(self):
        self.server.terminate()
        self.server.wait(timeout=10)

    def request(self, path, body=None, headers=None):
        req = Request(self.base + path,
                      data=None if body is None else json.dumps(body).encode(),
                      headers=headers or {'Content-Type': 'application/json'})
        with urlopen(req, timeout=10) as response:
            data = response.read()
            return response.status, data, response.headers

    def test_health_and_static(self):
        code, body, _ = self.request('/api/health')
        self.assertEqual(code, 200)
        self.assertTrue(json.loads(body)['ready'])
        self.assertIsNone(json.loads(body)['max_items'])
        _, html, headers = self.request('/')
        self.assertIn(b'BLADE', html)
        self.assertIn("frame-ancestors 'none'", headers['Content-Security-Policy'])

    def test_rejects_invalid_urls_and_cross_origin(self):
        for body, headers, expected in [
            ({'url': 'http://127.0.0.1/secret', 'mode': 'audio'}, None, 400),
            ({'url': 'https://youtube.com.evil.example/video', 'mode': 'video'}, None, 400),
            ({'url': 'https://youtube.com/watch?v=test', 'mode': 'invalid'}, None, 400),
            ({'url': 'https://youtube.com/watch?v=test', 'mode': 'audio'},
             {'Origin': 'https://other.example', 'Content-Type': 'application/json'}, 403),
        ]:
            with self.subTest(body=body), self.assertRaises(HTTPError) as caught:
                self.request('/api/jobs', body, headers)
            self.assertEqual(caught.exception.code, expected)

    def test_download_progress_and_valid_zip(self):
        code, body, _ = self.request('/api/jobs', {'url': 'https://www.youtube.com/playlist?list=TEST', 'mode': 'audio'})
        self.assertEqual(code, 202)
        job_id = json.loads(body)['id']
        for _ in range(100):
            _, body, _ = self.request('/api/jobs/' + job_id)
            job = json.loads(body)
            if job['state'] in ['complete', 'failed', 'partial']:
                break
            time.sleep(.1)
        self.assertEqual(job['state'], 'complete', job)
        self.assertIn('100.0%', job['log'])
        self.assertEqual(len(job['files']), 1)
        self.assertEqual(job['title'], 'List')
        self.assertEqual([(item['id'], item['state'], item.get('file')) for item in job['items']],
                         [('abc', 'done', 0), ('gone', 'failed', None)])
        _, content, headers = self.request(job['archive'])
        self.assertIn('attachment', headers['Content-Disposition'])
        with zipfile.ZipFile(io.BytesIO(content)) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(archive.read('001 - test [abc].mp3'), b'test media payload')
        _, media, _ = self.request(job['files'][0]['url'])
        self.assertEqual(media, b'test media payload')
        with self.assertRaises(HTTPError) as caught:
            self.request('/api/jobs', {'url': 'https://youtube.com/watch?v=test', 'mode': 'audio'})
        self.assertEqual(caught.exception.code, 429)
        for path, expected in [(f'/api/jobs/{job_id}/cancel', 409), ('/api/jobs/' + '0' * 32 + '/cancel', 404)]:
            with self.assertRaises(HTTPError) as caught:
                self.request(path, {})
            self.assertEqual(caught.exception.code, expected)


    def test_more_than_25_items_are_all_downloaded(self):
        _, body, _ = self.request('/api/jobs', {'url': 'https://youtube.com/playlist?list=LONG', 'mode': 'audio'})
        job_id = json.loads(body)['id']
        for _ in range(100):
            _, body, _ = self.request('/api/jobs/' + job_id)
            job = json.loads(body)
            if job['state'] in ['complete', 'partial', 'failed']:
                break
            time.sleep(.1)
        self.assertEqual(job['state'], 'complete', job)
        self.assertEqual(len(job['items']), 30)
        self.assertEqual(len(job['files']), 30)
        _, content, _ = self.request(job['archive'])
        with zipfile.ZipFile(io.BytesIO(content)) as archive:
            self.assertEqual(len(archive.infolist()), 30)
            self.assertIsNone(archive.testzip())
            self.assertEqual(archive.read('030 - test [vid30].mp3'), b'test media payload')

    def test_static_copy_on_the_allowed_origin_may_start_downloads(self):
        headers = {'Origin': 'https://site.example', 'Sec-Fetch-Site': 'cross-site',
                   'Content-Type': 'application/json'}
        code, _, response = self.request('/api/jobs', {'url': 'https://youtu.be/test', 'mode': 'audio'}, headers)
        self.assertEqual(code, 202)
        self.assertEqual(response['Access-Control-Allow-Origin'], 'https://site.example')
        _, _, response = self.request('/api/health')
        self.assertIsNone(response['Access-Control-Allow-Origin'])


if __name__ == '__main__':
    unittest.main()
