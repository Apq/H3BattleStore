"""Regenerate the two-row HB_bg from HA_bg's middle texture, without scaling."""
from pathlib import Path
from PIL import Image
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT.parent / "H3Auto" / "img" / "HA_bg.pcx"
TARGET = ROOT / "img" / "HB_bg.pcx"
PREVIEW = ROOT / "_hb_preview.png"
W, H = 480, 48
GOLD = (220, 200, 110)
DARK = (150, 130, 70)

with Image.open(SOURCE) as source:
    assert source.mode == "RGB", "Keep the original RGB PCX format"
    left, top = (source.width - W) // 2, (source.height - H) // 2
    texture = np.array(source.crop((left, top, left + W, top + H)))
pixels = texture.copy()
pixels[:2] = GOLD
pixels[-2:] = GOLD
pixels[:, :2] = GOLD
pixels[:, -2:] = GOLD
pixels[2, 2:-2] = DARK
pixels[-3, 2:-2] = DARK
# A baked separator between the two 24px rows, inside the outer frame.
pixels[23:25, 3:-3] = DARK
pixels[:, 2] = DARK
pixels[:, -3] = DARK
with Image.fromarray(pixels) as result:
    result.save(TARGET, format="PCX")
    result.save(PREVIEW, format="PNG")
with Image.open(TARGET) as check:
    assert check.mode == "RGB" and check.size == (W, H)
    np.testing.assert_array_equal(np.array(check), pixels)
    np.testing.assert_array_equal(pixels[3:23, 3:-3], texture[3:23, 3:-3])
    np.testing.assert_array_equal(pixels[25:-3, 3:-3], texture[25:-3, 3:-3])
assert tuple(pixels[0, 100]) == GOLD and tuple(pixels[2, 100]) == DARK
assert tuple(pixels[-1, 100]) == GOLD and tuple(pixels[-3, 100]) == DARK
print(f"PASS HB_bg.pcx: {W}x{H} RGB, centered HA crop ({left},{top}), exact texture and gold border")
