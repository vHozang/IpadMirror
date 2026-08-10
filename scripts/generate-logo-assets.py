from io import BytesIO
from pathlib import Path

from PIL import Image
from resvg_py import svg_to_bytes


ROOT = Path(__file__).resolve().parents[1]
ASSET_DIR = ROOT / "assets"
ICON_DIR = ASSET_DIR / "icons"
SOURCE = ASSET_DIR / "padmirror-logo.svg"
PREVIEW = ASSET_DIR / "padmirror-logo.png"


def render_source() -> Image.Image:
    rendered = svg_to_bytes(
        svg_path=str(SOURCE),
        width=2048,
        height=2048,
        text_rendering="optimize_legibility",
    )
    with Image.open(BytesIO(rendered)) as image:
        return image.convert("RGBA")


def main() -> None:
    ICON_DIR.mkdir(parents=True, exist_ok=True)
    source = render_source()
    resampling = Image.Resampling.LANCZOS

    source.resize((1600, 1600), resampling).save(PREVIEW, optimize=True)

    outputs = [PREVIEW]
    for size in (1024, 512, 256, 128, 64, 48, 32, 16):
        output = ICON_DIR / f"PadMirror-{size}.png"
        source.resize((size, size), resampling).save(output, optimize=True)
        outputs.append(output)

    ico_path = ICON_DIR / "PadMirror.ico"
    source.resize((256, 256), resampling).save(
        ico_path,
        format="ICO",
        sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)],
    )
    outputs.append(ico_path)

    for output in outputs:
        print(output.relative_to(ROOT))


if __name__ == "__main__":
    main()
