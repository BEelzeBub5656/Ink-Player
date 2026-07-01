"""Ink-Player voice ingress server.

Endpoints:
  POST /ink-player/api/voice_text   — JSON text upload (ESP32 local ASR result)
  POST /ink-player/api/voice_stream — chunked PCM audio stream → server-side ASR

Usage:
    python voice_text_server.py
    # Listens on 127.0.0.1:8460

Environment:
    INK_VOICE_TOKEN: Bearer token for ESP32 auth (required)
"""

import os
import io
import json
import struct
import uuid
import queue
import time
import asyncio
import threading
import logging
import subprocess
import urllib.request
import urllib.error
from datetime import datetime

from fastapi import FastAPI, Request, HTTPException
from fastapi.responses import JSONResponse, StreamingResponse

logging.basicConfig(level=logging.INFO, format="%(asctime)s [voice] %(message)s")
log = logging.getLogger("voice")

# ── 持久化 ASR 文本日志 ────────────────────────────────
ASR_LOG_DIR = "/var/log/ink-player"
ASR_LOG_FILE = os.path.join(ASR_LOG_DIR, "asr_text.log")
os.makedirs(ASR_LOG_DIR, exist_ok=True)

def _append_asr_log(device_id: str, source: str, text: str, ts: float) -> None:
    """Append one line to the persistent ASR text log."""
    line = json.dumps({
        "ts": datetime.fromtimestamp(ts).isoformat(),
        "device": device_id,
        "source": source,
        "text": text,
    }, ensure_ascii=False)
    with open(ASR_LOG_FILE, "a", encoding="utf-8") as f:
        f.write(line + "\n")

BEARER_TOKEN = os.environ.get("INK_VOICE_TOKEN", "ink-player-v1-token-change-me")

# SSE 连接池
_sse_queues: list[queue.Queue] = []

def _broadcast_sse(data: dict) -> None:
    """Push event to all connected SSE clients."""
    msg = json.dumps(data, ensure_ascii=False)
    for q in _sse_queues:
        try:
            q.put_nowait(msg)
        except queue.Full:
            pass

# 豆包/火山 Flash ASR 配置 (极速版，同步返回)
# 新版控制台：只需要 X-Api-Key + X-Api-Resource-Id
DOUBAO_ASR_APP_ID = "826048352"
DOUBAO_ASR_API_KEY = "ba6b8007-5bbb-4df0-a4a2-ca5675fe8639"
DOUBAO_ASR_URL = "https://openspeech.bytedance.com/api/v3/auc/bigmodel/recognize/flash"
DOUBAO_ASR_RESOURCE = "volc.bigasr.auc_turbo"

# v1 限制
MAX_AUDIO_BYTES = 512 * 1024     # 512KB raw PCM (~16s @16kHz mono)
MAX_AUDIO_SECS = 15

app = FastAPI(title="Ink-Player Voice Ingress")


# ── Auth ──────────────────────────────────────────────

def validate_token(auth_header: str | None) -> bool:
    if not auth_header or not auth_header.startswith("Bearer "):
        return False
    return auth_header.removeprefix("Bearer ").strip() == BEARER_TOKEN


# ── WAV helpers ───────────────────────────────────────

def pcm_to_wav(pcm: bytes, sample_rate: int = 16000, channels: int = 1) -> bytes:
    """Wrap raw PCM s16le in a WAV container."""
    bits = 16
    byte_rate = sample_rate * channels * bits // 8
    block_align = channels * bits // 8
    data_size = len(pcm)

    buf = io.BytesIO()
    buf.write(b"RIFF")
    buf.write(struct.pack("<I", 36 + data_size))
    buf.write(b"WAVE")
    buf.write(b"fmt ")
    buf.write(struct.pack("<I", 16))            # fmt chunk size
    buf.write(struct.pack("<H", 1))             # PCM
    buf.write(struct.pack("<H", channels))
    buf.write(struct.pack("<I", sample_rate))
    buf.write(struct.pack("<I", byte_rate))
    buf.write(struct.pack("<H", block_align))
    buf.write(struct.pack("<H", bits))
    buf.write(b"data")
    buf.write(struct.pack("<I", data_size))
    buf.write(pcm)
    return buf.getvalue()


# ── 豆包 Flash ASR ────────────────────────────────────

def call_doubao_asr(wav: bytes) -> str | None:
    """POST WAV (base64) to 豆包 Flash ASR (极速版), return recognized text."""
    import base64
    payload = {
        "user": {"uid": DOUBAO_ASR_APP_ID},
        "audio": {"data": base64.b64encode(wav).decode()},
        "request": {"model_name": "bigmodel"},
    }
    try:
        req = urllib.request.Request(
            DOUBAO_ASR_URL,
            data=json.dumps(payload).encode(),
            headers={
                "Content-Type": "application/json",
                "X-Api-Key": DOUBAO_ASR_API_KEY,
                "X-Api-Resource-Id": DOUBAO_ASR_RESOURCE,
                "X-Api-Request-Id": str(uuid.uuid4()),
                "X-Api-Sequence": "-1",
            },
            method="POST",
        )
        with urllib.request.urlopen(req, timeout=30) as resp:
            result = json.loads(resp.read().decode())
            code = resp.headers.get("X-Api-Status-Code", "")
            log.info(f"ASR response: status={code}, has_text={'text' in str(result)}")
            # DEBUG: dump raw response for field inspection
            log.info(f"ASR raw keys: {list(result.keys())}  result keys: {list(result.get('result', {}).keys()) if isinstance(result.get('result'), dict) else type(result.get('result')).__name__}")
            text = result.get("result", {}).get("text", "").strip()
            if not text and result.get("result", {}).get("utterances"):
                text = result["result"]["utterances"][0].get("text", "").strip()
            return text or None
    except urllib.error.HTTPError as e:
        body = e.read().decode()[:300]
        log.warning(f"ASR HTTP {e.code}: {body}")
        return None
    except Exception as e:
        log.error(f"ASR call failed: {e}")
        return None


# ── Hermes memory ─────────────────────────────────────

def store_in_hermes(text: str, device_id: str, source: str, ts: int) -> str | None:
    """Write recognized text to Hermes memory via one-shot CLI."""
    event_id = str(uuid.uuid4())
    dt = datetime.fromtimestamp(ts) if ts else datetime.now()

    prompt = (
        f"保存这条语音识别结果到记忆: "
        f"用户通过Ink-Player墨水屏设备说了: 「{text}」。"
        f"设备ID: {device_id}, 来源: {source}, "
        f"时间: {dt.strftime('%Y-%m-%d %H:%M:%S')}"
    )

    try:
        env = os.environ.copy()
        result = subprocess.run(
            ["hermes", "-z", prompt, "--yolo", "-t", ""],
            capture_output=True, text=True, timeout=10,
            env=env, cwd="/root",
        )
        if result.returncode == 0:
            log.info(f"Stored in Hermes: {text[:50]}... (event={event_id})")
            return event_id
        else:
            log.warning(f"Hermes one-shot failed: {result.stderr[:200]}")
            return None
    except Exception as e:
        log.error(f"Hermes unreachable: {e}")
        return None


# ── Endpoints ─────────────────────────────────────────

@app.post("/ink-player/api/voice_text")
async def voice_text(request: Request):
    """Accept recognized speech text from ESP32 (JSON).

    Headers:
        Authorization: Bearer <token>
        Content-Type: application/json
    Body:
        {"device_id":"ink-player-v1","source":"esp32_flash_asr","text":"你好","ts":1780000000}
    """
    auth = request.headers.get("Authorization")
    if not validate_token(auth):
        raise HTTPException(status_code=401, detail="Invalid token")

    try:
        body = await request.json()
    except Exception:
        raise HTTPException(status_code=400, detail="Invalid JSON body")

    device_id = body.get("device_id", "").strip()
    source = body.get("source", "").strip()
    text = body.get("text", "").strip()
    ts = body.get("ts", 0)

    if not device_id or not source or not text:
        raise HTTPException(status_code=400, detail="Missing required fields")

    log.info(f"text [{device_id}/{source}]: {text[:80]}")
    _broadcast_sse({"text": text, "source": source, "device": device_id, "at": datetime.now().isoformat()})
    _append_asr_log(device_id, source, text, ts or time.time())
    event_id = store_in_hermes(text, device_id, source, ts)

    return JSONResponse(content={
        "ok": True,
        "event_id": event_id or "local-only",
        "display_text": "",
    })


@app.post("/ink-player/api/voice_stream")
async def voice_stream(request: Request):
    """Accept chunked PCM audio from ESP32, run server-side ASR.

    Headers:
        Authorization: Bearer <token>
        Content-Type: audio/L16; rate=16000; channels=1
        Transfer-Encoding: chunked
        X-Ink-Device-Id: ink-player-v1
        X-Ink-Source: esp32_i2s_pcm
        X-Ink-Sample-Rate: 16000
        X-Ink-Channels: 1
        X-Ink-Format: pcm_s16le

    Body: raw PCM s16le chunks, total max ~512KB / 15s.
    Returns: {"ok":true, "event_id":"...", "text":"识别文本", "display_text":""}
    """
    auth = request.headers.get("Authorization")
    if not validate_token(auth):
        raise HTTPException(status_code=401, detail="Invalid token")

    device_id = request.headers.get("X-Ink-Device-Id", "ink-player-v1").strip()
    source = request.headers.get("X-Ink-Source", "esp32_i2s_pcm").strip()
    try:
        sample_rate = int(request.headers.get("X-Ink-Sample-Rate", "16000"))
        channels = int(request.headers.get("X-Ink-Channels", "1"))
    except (ValueError, TypeError):
        raise HTTPException(status_code=400, detail="Invalid sample rate or channels header")

    # Stream body in
    chunks: list[bytes] = []
    total = 0
    async for chunk in request.stream():
        chunks.append(chunk)
        total += len(chunk)
        if total > MAX_AUDIO_BYTES:
            log.warning(f"stream [{device_id}]: exceeded max {MAX_AUDIO_BYTES} bytes")
            raise HTTPException(status_code=413, detail="Audio too large")

    pcm = b"".join(chunks)
    if total < 1600:  # < 50ms — too short
        raise HTTPException(status_code=400, detail="Audio too short")

    log.info(f"stream [{device_id}]: {total} bytes PCM ({total / sample_rate / 2:.1f}s) -> ASR")

    # PCM → WAV
    wav = pcm_to_wav(pcm, sample_rate=sample_rate, channels=channels)

    # ASR
    text = call_doubao_asr(wav)
    if not text:
        return JSONResponse(content={
            "ok": True,
            "event_id": None,
            "text": "",
            "display_text": "",
        })

    log.info(f"stream [{device_id}]: ASR → 「{text}」")
    _broadcast_sse({"text": text, "source": source, "device": device_id, "at": datetime.now().isoformat()})
    _append_asr_log(device_id, source, text, time.time())

    # Hermes memory — fire-and-forget (don't block the HTTP response)
    ts = int(datetime.now().timestamp())
    threading.Thread(
        target=store_in_hermes, args=(text, device_id, source, ts),
        daemon=True
    ).start()

    return JSONResponse(content={
        "ok": True,
        "event_id": "async-pending",
        "text": text,
        "display_text": "",
    })


@app.get("/ink-player/events")
async def sse_events(request: Request):
    """GET /ink-player/events — Server-Sent Events stream."""
    q: queue.Queue = queue.Queue()
    _sse_queues.append(q)

    async def event_stream():
        try:
            yield "data: {}\n\n"
            while True:
                if await request.is_disconnected():
                    break
                try:
                    msg = q.get(timeout=15)
                    yield f"data: {msg}\n\n"
                except queue.Empty:
                    yield ": keepalive\n\n"
        finally:
            _sse_queues.remove(q)

    return StreamingResponse(
        event_stream(),
        media_type="text/event-stream",
        headers={"Cache-Control": "no-cache", "X-Accel-Buffering": "no"},
    )


@app.get("/health")
async def health():
    return {"status": "ok"}


# ── 图片上传 + 三色抖动 ────────────────────────────────

from fastapi import UploadFile, File, Form

IMAGES_DIR = "/var/www/ink-player/images/uploaded"
os.makedirs(IMAGES_DIR, exist_ok=True)


@app.post("/ink-player/api/image/upload")
async def image_upload(
    request: Request,
    file: UploadFile = File(...),
):
    """POST /ink-player/api/image/upload
    
    上传任意图片 → Floyd-Steinberg 三色抖动 (880×528) → 保存 → 返回 URL
    
    微信小程序调用示例:
      wx.uploadFile({
        url: 'https://beelzebub.top/ink-player/api/image/upload',
        filePath: tempFilePath,
        name: 'file',
        header: { 'Authorization': 'Bearer <token>' },
        success: res => { const data = JSON.parse(res.data); ... }
      })
    """
    auth = request.headers.get("Authorization")
    if not validate_token(auth):
        raise HTTPException(status_code=401, detail="Invalid token")

    # 读上传的图片
    contents = await file.read()
    if len(contents) > 10 * 1024 * 1024:  # 10MB 上限
        raise HTTPException(status_code=413, detail="Image too large (>10MB)")

    # PIL 打开
    from PIL import Image
    import numpy as np
    import io as stdio

    img = Image.open(stdio.BytesIO(contents)).convert("RGB")

    # 缩放到 880×528（保持比例，居中裁剪）
    tw, th = 880, 528
    img_ratio = img.width / img.height
    target_ratio = tw / th
    if img_ratio > target_ratio:
        new_w = int(img.height * target_ratio)
        img = img.crop(((img.width - new_w) // 2, 0, (img.width + new_w) // 2, img.height))
    else:
        new_h = int(img.width / target_ratio)
        img = img.crop((0, (img.height - new_h) // 2, img.width, (img.height + new_h) // 2))
    img = img.resize((tw, th), Image.LANCZOS)

    # Floyd-Steinberg 三色抖动
    arr = np.array(img, dtype=np.float32)
    pal = np.array([[245, 240, 232], [34, 34, 34], [192, 57, 43]], dtype=np.float32)
    h, w, _ = arr.shape
    for y in range(h):
        for x in range(w):
            old = arr[y, x].copy()
            idx = np.argmin(np.sum((pal - old) ** 2, axis=1))
            err = old - pal[idx]
            arr[y, x] = pal[idx]
            if x + 1 < w:
                arr[y, x + 1] += err * 7 / 16
            if y + 1 < h:
                if x > 0:
                    arr[y + 1, x - 1] += err * 3 / 16
                arr[y + 1, x] += err * 5 / 16
                if x + 1 < w:
                    arr[y + 1, x + 1] += err * 1 / 16
    result = Image.fromarray(arr.clip(0, 255).astype(np.uint8))

    # 保存
    img_id = str(uuid.uuid4())[:8]
    filename = f"{img_id}.png"
    filepath = os.path.join(IMAGES_DIR, filename)
    result.save(filepath, "PNG")

    # 可选：MQTT 推送给 ESP32
    # img_url = f"https://beelzebub.top/ink-player/images/uploaded/{filename}"
    # mqtt_publish("desk/display", json.dumps({"image_url": img_url}))

    log.info(f"image_upload: {file.filename} → {filename} ({tw}×{th})")

    return JSONResponse(content={
        "ok": True,
        "image_id": img_id,
        "url": f"https://beelzebub.top/ink-player/images/uploaded/{filename}",
        "width": tw,
        "height": th,
    })


# ── DLNA 音频转码代理 ──────────────────────────────────

@app.get("/ink-player/api/audio_proxy.wav")
def audio_proxy(request: Request):
    """GET /ink-player/api/audio_proxy.wav?url=<percent-encoded-media-url>
    
    将任意 HTTP 媒体 URL（MP3/AAC/FLAC 等）通过 ffmpeg 实时转码为
    16kHz mono 16-bit WAV，流式返回给 ESP32 播放器。
    """
    raw_url = request.query_params.get("url", "")
    if not raw_url:
        raise HTTPException(status_code=400, detail="Missing url parameter")

    # 限制只允许 http/https，防止 SSRF
    if not raw_url.startswith(("http://", "https://")):
        raise HTTPException(status_code=400, detail="Only http/https URLs supported")

    log.info(f"audio_proxy: {raw_url[:150]}")

    def generate():
        proc = None
        try:
            proc = subprocess.Popen(
                [
                    "ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin",
                    "-timeout", "15000000",   # 15s 连接超时 (微秒)
                    "-i", raw_url,
                    "-vn", "-ac", "1", "-ar", "16000", "-f", "wav", "pipe:1",
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
            )
            for chunk in iter(lambda: proc.stdout.read(65536), b""):
                yield chunk
        except GeneratorExit:
            pass  # 客户端断开 → finally 杀 ffmpeg
        except Exception as e:
            log.warning(f"audio_proxy error: {e}")
        finally:
            if proc:
                proc.kill()
                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    pass

    return StreamingResponse(
        generate(),
        media_type="audio/wav",
        headers={
            "Cache-Control": "no-store",
            "X-Accel-Buffering": "no",
        },
    )


if __name__ == "__main__":
    import uvicorn
    log.info(f"Starting voice server on 127.0.0.1:8460, token={'*'*8}")
    uvicorn.run(app, host="127.0.0.1", port=8460)
