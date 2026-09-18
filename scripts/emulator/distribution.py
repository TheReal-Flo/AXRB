"""Validate bundled runtime binaries without requiring a developer toolchain."""
import hashlib
import json


def bundled_library(root, relative):
    manifest = root / 'distribution.json'
    if not manifest.exists():
        return None
    expected = json.loads(manifest.read_text(encoding='utf-8'))['sha256'].get(relative)
    library = root / relative
    if not expected or not library.is_file() or hashlib.sha256(library.read_bytes()).hexdigest() != expected:
        raise RuntimeError('Bundled runtime is damaged. Reinstall AXRB.')
    return library
