import sys
import io
import os
import struct
import socketserver
import numpy as np
from pathlib import Path
from PIL import Image
import torch
from transformers import VitMatteForImageMatting, VitMatteImageProcessor

MODEL_TAG = "vitsmall_dist646"
MODEL_ID  = "hustvl/vitmatte-small-distinctions-646"
MODEL_DIR = Path(__file__).parent / "models" / f"vitmatte_model_{MODEL_TAG}"

processor = None
model     = None

def recv_all(sock, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("Client disconnected")
        buf.extend(chunk)
    return bytes(buf)

def recv_blob(sock):
    size = struct.unpack(">I", recv_all(sock, 4))[0]
    return recv_all(sock, size)

def send_blob(sock, data):
    sock.sendall(struct.pack(">I", len(data)))
    sock.sendall(data)

def send_progress(sock, message: str):
    """Send a progress string. Type byte 'P' + length-prefixed UTF-8 string."""
    data = message.encode("utf-8")
    sock.sendall(b'P')
    send_blob(sock, data)

def send_result(sock, data: bytes):
    """Send final result. Type byte 'R' + length-prefixed blob."""
    sock.sendall(b'R')
    send_blob(sock, data)

def run_matte(rgb_bytes, tri_bytes, progress_cb=None):
    def prog(msg):
        print(msg, flush=True)
        if progress_cb:
            progress_cb(msg)

    prog("Step 1/4: Decoding images...")
    image      = Image.open(io.BytesIO(rgb_bytes)).convert("RGB")
    trimap_raw = Image.open(io.BytesIO(tri_bytes)).convert("L")
    orig_size  = image.size

    prog("Step 2/4: Posterizing trimap...")
    trimap_np  = np.array(trimap_raw, dtype=np.uint8)
    posterized = np.zeros_like(trimap_np)
    posterized[trimap_np >= 192] = 255
    posterized[(trimap_np >= 64) & (trimap_np < 192)] = 128
    trimap = Image.fromarray(posterized, mode="L")

    w, h   = image.size
    w      = (w // 32) * 32
    h      = (h // 32) * 32
    image  = image.resize((w, h))
    trimap = trimap.resize((w, h))

    prog("Step 3/4: Running model inference...")
    inputs = processor(images=image, trimaps=trimap, return_tensors="pt")
    with torch.no_grad():
        outputs = model(**inputs)

    prog("Step 4/4: Encoding result...")
    alpha     = outputs.alphas[0, 0].cpu().numpy()
    alpha_img = Image.fromarray((alpha * 255).astype(np.uint8), mode="L")
    alpha_img = alpha_img.resize(orig_size, Image.BILINEAR)

    buf = io.BytesIO()
    alpha_img.save(buf, format="PNG")
    return buf.getvalue()

class MatteHandler(socketserver.BaseRequestHandler):
    def handle(self):
        try:
            rgb_bytes = recv_blob(self.request)
            tri_bytes = recv_blob(self.request)

            def progress_cb(msg):
                send_progress(self.request, msg)

            result = run_matte(rgb_bytes, tri_bytes, progress_cb=progress_cb)
            send_result(self.request, result)
            print(f"OK  {self.client_address}  out={len(result)} bytes", flush=True)
        except Exception as e:
            print(f"ERR {self.client_address}: {e}", flush=True)

def download_model():
    MODEL_DIR.mkdir(parents=True, exist_ok=True)
    print(f"Downloading {MODEL_ID} ...", flush=True)
    VitMatteImageProcessor.from_pretrained(MODEL_ID).save_pretrained(str(MODEL_DIR))
    VitMatteForImageMatting.from_pretrained(MODEL_ID).save_pretrained(str(MODEL_DIR))
    print(f"Saved to {MODEL_DIR}", flush=True)

def load_model():
    global processor, model
    print(f"Loading {MODEL_TAG} ...", flush=True)
    processor = VitMatteImageProcessor.from_pretrained(str(MODEL_DIR))
    model     = VitMatteForImageMatting.from_pretrained(str(MODEL_DIR))
    model.eval()
    print("Model loaded.", flush=True)

if __name__ == "__main__":
    if not MODEL_DIR.exists():
        download_model()
    load_model()

    HOST, PORT = "127.0.0.1", 8000
    with socketserver.TCPServer((HOST, PORT), MatteHandler) as srv:
        srv.allow_reuse_address = True
        print(f"Listening on {HOST}:{PORT}", flush=True)
        srv.serve_forever()
