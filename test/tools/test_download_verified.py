import hashlib

import pytest

from download_verified import download_verified


def test_download_verified_writes_digest_matched_file(tmp_path):
    source = tmp_path / "source.bin"
    source.write_bytes(b"official firmware")
    output = tmp_path / "cache" / "firmware.bin"
    digest = hashlib.sha256(source.read_bytes()).hexdigest()

    download_verified(source.as_uri(), digest, output)

    assert b"official firmware" == output.read_bytes()


def test_download_verified_rejects_wrong_digest_without_publishing(tmp_path):
    source = tmp_path / "source.bin"
    source.write_bytes(b"unexpected firmware")
    output = tmp_path / "firmware.bin"

    with pytest.raises(RuntimeError, match="SHA-256 mismatch"):
        download_verified(source.as_uri(), "0" * 64, output)

    assert not output.exists()
