import torch
import numpy as np
from transformers import VitMatteForImageMatting

# Load your model (CPU only, we don't need GPU for export)
model = VitMatteForImageMatting.from_pretrained("hustvl/vitmatte-small-composition-1k")
model.eval()
model.cpu()

# Dummy inputs matching what your server actually sends
# VitMatte expects: pixel_values [B, 3, H, W] + trimap [B, 1, H, W]
# Using 512x512 as example - check what resolution you actually use
batch_size = 1
H, W = 512, 512

dummy_pixel_values = torch.randn(batch_size, 3, H, W)
dummy_trimap = torch.randn(batch_size, 1, H, W)

# Export
torch.onnx.export(
    model,
    args=(dummy_pixel_values, dummy_trimap),  # positional inputs
    f="vitmatte.onnx",
    opset_version=14,           # 14+ recommended for modern models
    input_names=["pixel_values", "trimap"],
    output_names=["alphas"],
    dynamic_axes={              # allows any image size at runtime, not just 512x512
        "pixel_values": {0: "batch", 2: "height", 3: "width"},
        "trimap":        {0: "batch", 2: "height", 3: "width"},
        "alphas":        {0: "batch", 2: "height", 3: "width"},
    },
    do_constant_folding=True,   # folds constant ops at export time = faster inference
)

print("Done. Verify the export works:")

# Quick sanity check - load it back and run it
import onnxruntime as ort

sess = ort.InferenceSession("vitmatte.onnx", providers=["CPUExecutionProvider"])

outputs = sess.run(
    ["alphas"],
    {
        "pixel_values": dummy_pixel_values.numpy(),
        "trimap": dummy_trimap.numpy(),
    }
)

print(f"Output shape: {outputs[0].shape}")   # should be [1, 1, 512, 512]
print(f"Value range: {outputs[0].min():.3f} to {outputs[0].max():.3f}")  # should be ~0..1
print("Export verified OK")
