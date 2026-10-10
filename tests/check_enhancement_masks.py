"""Check the actual prepared zone weights and road-clearance masks."""
import json
from pathlib import Path
import numpy as np
from PIL import Image

root = Path(__file__).parent / "build"
for zone in ("000", "001"):
    folder = root / ("enhancement" + zone)
    weights = np.asarray(Image.open(folder / "terrain-weights.png"), dtype=np.int32)
    assert weights.shape == (1024, 1024, 4)
    error = int(np.abs(weights.sum(axis=2) - 255).max())
    assert error <= 2, "Weights lost their normalized color coverage"
    data = json.loads((folder / "ground-cover.json").read_text())
    assert data["width"] == 512 and len(data["samples"]) == 512*512
    assert min(data["samples"]) >= 0 and max(data["samples"]) <= 255
    mask = np.array(data["samples"], dtype=np.int32).reshape((512,512))
    source = weights[:,:,0].reshape(512,2,512,2).mean(axis=(1,3))
    assert np.all(mask <= source+1), "Grass-mask erosion grew onto a non-grass surface"
    assert np.any(mask == 0) and np.any(mask >= 184)
    print(f"Zone {zone}: normalized weights and conservative grass mask passed (max weight error {error}).")
