"""TTS service using Edge-TTS.

Uses the edge-tts CLI to synthesize speech from text.
"""

import asyncio
import subprocess
import os


def synthesize(text: str, output_path: str = "output.mp3") -> str:
    """Synthesize text to speech using Edge-TTS.

    Args:
        text: The text to synthesize. Supports Chinese and other languages.
        output_path: Path where the MP3 file will be saved.

    Returns:
        The path to the generated MP3 file.

    Raises:
        ValueError: If text is empty.
        RuntimeError: If edge-tts CLI fails.
    """
    if not text or not text.strip():
        raise ValueError("Text cannot be empty")

    # Run edge-tts as a subprocess (CLI interface)
    cmd = [
        "edge-tts",
        "--voice", "zh-CN-XiaoxiaoNeural",  # Chinese female voice
        "--text", text,
        "--write-media", output_path,
    ]

    result = subprocess.run(cmd, capture_output=True, text=True)

    if result.returncode != 0:
        raise RuntimeError(
            f"edge-tts failed with exit code {result.returncode}: "
            f"{result.stderr.strip()}"
        )

    return output_path
