"""Tests for the TTS service using Edge-TTS."""

import os
import tempfile
import pytest
from services.tts_service import synthesize


def test_synthesize_creates_mp3_file():
    """Test that synthesize() creates a valid MP3 file for Chinese text."""
    with tempfile.NamedTemporaryFile(suffix=".mp3", delete=False) as f:
        output_path = f.name

    try:
        result = synthesize("你好世界", output_path=output_path)
        assert result == output_path, "Should return the output file path"
        assert os.path.exists(output_path), "Output file should exist"
        assert os.path.getsize(output_path) > 0, "Output file should not be empty"
        # MP3 files start with ID3 tag or sync word (0xFF 0xFB or 0xFF 0xF3)
        with open(output_path, "rb") as f:
            header = f.read(4)
            assert header[:2] == b"\xff\xfb" or header[:2] == b"\xff\xf3" or header[:3] == b"ID3", \
                f"File header {header.hex()} does not look like MP3"
    finally:
        if os.path.exists(output_path):
            os.unlink(output_path)


def test_synthesize_returns_path():
    """Test that synthesize returns the correct file path."""
    with tempfile.NamedTemporaryFile(suffix=".mp3", delete=False) as f:
        output_path = f.name

    try:
        result = synthesize("Hello world", output_path=output_path)
        assert isinstance(result, str)
        assert result == output_path
    finally:
        if os.path.exists(output_path):
            os.unlink(output_path)


def test_synthesize_empty_text():
    """Test that empty text raises an error."""
    with pytest.raises(ValueError, match="Text cannot be empty"):
        synthesize("", output_path="test.mp3")
