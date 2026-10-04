"""Cross the classic ZIP entry-count boundary with a small deterministic archive."""
import subprocess
import sys
import tempfile
from pathlib import Path
import zipfile

with tempfile.TemporaryDirectory() as folder:
    subprocess.run([sys.argv[1], folder], check=True)
    with zipfile.ZipFile(Path(folder) / 'many.zip') as archive:
        assert len(archive.infolist()) == 65536
        assert all(entry.extract_version == 45 for entry in archive.infolist())
        assert archive.testzip() is None
        assert archive.read('empty.txt') == b''
print('ZIP64: 65,536 entries read and verified by Python zipfile.')
