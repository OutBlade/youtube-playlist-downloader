"""Exercise real process launching without downloading media."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

APP, ENGINE = map(lambda p: str(Path(p).resolve()), sys.argv[1:3])
sys.argv = sys.argv[:1]
URL = 'https://www.youtube.com/playlist?list=PLtest&index=1'


class CLI(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.log = self.root / 'args.txt'
        self.env = dict(os.environ, YTPLAYLIST_TEST_LOG=str(self.log))
        engine_dir = self.root / 'engine with spaces'
        engine_dir.mkdir()
        self.engine = str(engine_dir / Path(ENGINE).name)
        shutil.copy2(ENGINE, self.engine)

    def run_app(self, *args, status='0'):
        return subprocess.run([APP, *args], cwd=self.root,
                              env=dict(self.env, YTPLAYLIST_TEST_EXIT=status),
                              capture_output=True, encoding='utf-8', errors='replace')

    def test_help(self):
        result = self.run_app('--help')
        self.assertEqual(result.returncode, 0)
        self.assertIn('Usage:', result.stdout)
        self.assertFalse(self.log.exists())

    def test_invalid_arguments_never_launch(self):
        for args in [[], [URL, '-j', '0'], [URL, '-j', '33'], [URL, '-j', '1x'],
                     [URL, '-o'], [URL, '--bad'], [URL, URL],
                     ['https://youtube.com.evil.example/playlist?list=x']]:
            with self.subTest(args=args):
                self.assertEqual(self.run_app(*args).returncode, 2)
                self.assertFalse(self.log.exists())

    def test_video_arguments_and_quoting(self):
        folder = 'Müsik & videos' if os.name == 'nt' else 'Müsik & "videos"'
        url = URL + '&title="quoted"\\tail'
        result = self.run_app(url, '--yt-dlp', self.engine, '-o', folder, '-j', '12')
        self.assertEqual(result.returncode, 0, result.stderr)
        args = self.log.read_text(encoding='utf-8').splitlines()
        self.assertEqual(args[-2:], ['--', url])
        self.assertEqual(args[args.index('--concurrent-fragments') + 1], '12')
        self.assertEqual(Path(args[args.index('--paths') + 1]), self.root / folder)
        self.assertEqual(Path(args[args.index('--download-archive') + 1]),
                         self.root / folder / 'downloaded.txt')
        self.assertIn('--yes-playlist', args)
        self.assertIn('--continue', args)
        self.assertNotIn('--extract-audio', args)

    def test_audio_and_failure_propagation(self):
        result = self.run_app(URL, '--yt-dlp', ENGINE, '--audio', status='7')
        self.assertEqual(result.returncode, 7)
        args = self.log.read_text(encoding='utf-8').splitlines()
        self.assertIn('--extract-audio', args)
        self.assertEqual(args[args.index('--audio-format') + 1], 'mp3')
        self.assertIn('Some items may have failed', result.stderr)

    def test_missing_engine(self):
        result = self.run_app(URL, '--yt-dlp', str(self.root / 'missing-engine'))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Could not start yt-dlp', result.stderr)


if __name__ == '__main__':
    unittest.main()
