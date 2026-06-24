import sys
import io
import os
import numpy as np
from pathlib import Path
from fastapi import FastAPI, UploadFile, File
from fastapi.responses import Response
import uvicorn
from PIL import Image
import torch
from transformers import VitMatteForImageMatting, VitMatteImageProcessor

MODEL_TAG = "vitsmall_dist646"
MODEL_ID  = "hustvl/vitmatte-small-distinctions-646"
MODEL_DIR = Path(__file__).parent / "models" / f"vitmatte_model_{MODEL_TAG}"

app = FastAPI()

processor = None
model     = None

def download_model():
    MODEL_DIR.mkdir(parents=True, exist_ok=True)
    print(f"Downloading {MODEL_ID} ...", flush=True)
    proc_dl  = VitMatteImageProcessor.from_pretrained(MODEL_ID)
    model_dl = VitMatteForImageMatting.from_pretrained(MODEL_ID)
    proc_dl.save_pretrained(str(MODEL_DIR))
    model_dl.save_pretrained(str(MODEL_DIR))
    print(f"Saved to {MODEL_DIR}", flush=True)

def load_model():
    global processor, model
    print(f"Loading {MODEL_TAG} ...", flush=True)
    processor = VitMatteImageProcessor.from_pretrained(str(MODEL_DIR))
    model     = VitMatteForImageMatting.from_pretrained(str(MODEL_DIR))
    model.eval()
    print("Model loaded.", flush=True)

@app.get("/health")
def health():
    return {"status": "ok", "model_loaded": model is not None}

@app.post("/matte")
async def matte(
    rgb:     UploadFile = File(...),
    trimask: UploadFile = File(...)
):
    # --- load inputs ---
    rgb_bytes = await rgb.read()
    tri_bytes = await trimask.read()

    image  = Image.open(io.BytesIO(rgb_bytes)).convert("RGB")
    trimap_raw = Image.open(io.BytesIO(tri_bytes)).convert("L")

    orig_size = image.size

    # posterize trimap
    trimap_np = np.array(trimap_raw, dtype=np.uint8)
    posterized = np.zeros_like(trimap_np)
    posterized[trimap_np >= 192] = 255
    posterized[(trimap_np >= 64) & (trimap_np < 192)] = 128
    trimap = Image.fromarray(posterized, mode="L")

    # resize to multiple of 32
    w, h = image.size
    w = (w // 32) * 32
    h = (h // 32) * 32
    image  = image.resize((w, h))
    trimap = trimap.resize((w, h))

    # --- inference ---
    inputs = processor(images=image, trimaps=trimap, return_tensors="pt")
    with torch.no_grad():
        outputs = model(**inputs)

    alpha = outputs.alphas[0, 0].cpu().numpy()
    alpha_img = Image.fromarray((alpha * 255).astype(np.uint8), mode="L")
    alpha_img = alpha_img.resize(orig_size, Image.BILINEAR)

    # --- encode as PNG 8-bit grayscale ---
    buf = io.BytesIO()
    alpha_img.save(buf, format="PNG")
    buf.seek(0)

    return Response(content=buf.read(), media_type="image/png")

if __name__ == "__main__":
    if not MODEL_DIR.exists():
        download_model()
    load_model()
    uvicorn.run(app, host="127.0.0.1", port=8000)
