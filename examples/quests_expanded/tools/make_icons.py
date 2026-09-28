"""Makes the Quests Expanded mod's icons (quests_expanded/icon_*.png, materials/icon-quests_expanded_*.mat).

Drawn like the game's own icons: a flat white shape, 128 pixels square on transparent, its lower part a shade darker.
The quest card's arrows stand on a colored strip where its number was (quests_expanded_hand_off_backing.mat in red,
quests_expanded_branch_backing.mat in orange, quests_expanded_hand_in_backing.mat in green): a hand-off's arrow points
left, a branch quest's splits into two pointing left, a hand-in's points right, and one that starts a chapter comes from
a bar. Run from anywhere:

    python make_icons.py

Needs Pillow.
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw

MOD = Path(__file__).resolve().parent.parent / "mod"
SIZE = 128
SCALE = 4
BIG = SIZE * SCALE

WHITE = (255, 255, 255)

MATERIAL = """Material{
	name "icon-quests_expanded_{name}"
	mode normal
	color 1.0 1.0 1.0 1.0
	culling none
	zread false
	zwrite false
	glow false
	texture "quests_expanded/icon_{name}.png" clampUV
}
"""


def shaded(color, amount: float = 0.78):
    return tuple(int(channel * amount) for channel in color)


def canvas():
    image = Image.new("RGBA", (BIG, BIG), (0, 0, 0, 0))

    return image, ImageDraw.Draw(image)


# The shape's lower part a shade darker, as the game's icons are lit from above.
def two_tone(shape: Image.Image, color) -> Image.Image:
    mask = shape.getchannel("A")
    top = Image.new("RGBA", (BIG, BIG), (*color, 255))
    bottom = Image.new("RGBA", (BIG, BIG), (*shaded(color), 255))
    lower = Image.new("L", (BIG, BIG), 0)
    ImageDraw.Draw(lower).rectangle((0, BIG * 0.56, BIG, BIG), fill=255)

    result = Image.composite(bottom, top, lower)
    result.putalpha(mask)

    return result


def arrow(pointing_right: bool) -> Image.Image:
    image, draw = canvas()
    shaft = [(0.12, 0.40), (0.56, 0.40), (0.56, 0.20), (0.90, 0.50), (0.56, 0.80), (0.56, 0.60), (0.12, 0.60)]

    if not pointing_right:
        shaft = [(1.0 - x, y) for x, y in shaft]

    draw.polygon([(x * BIG, y * BIG) for x, y in shaft], fill=(255, 255, 255, 255))

    return image


# A hand-in that starts a chapter: the arrow from a bar, as a new start.
def chapter_arrow() -> Image.Image:
    image, draw = canvas()
    shaft = [(0.30, 0.40), (0.62, 0.40), (0.62, 0.20), (0.92, 0.50), (0.62, 0.80), (0.62, 0.60), (0.30, 0.60)]

    draw.polygon([(x * BIG, y * BIG) for x, y in shaft], fill=(255, 255, 255, 255))
    draw.rectangle((0.10 * BIG, 0.18 * BIG, 0.21 * BIG, 0.82 * BIG), fill=(255, 255, 255, 255))

    return image


# A branch quest: one shaft from the right splitting into two arrows pointing left, as a hand-off's does.
def branch_arrows() -> Image.Image:
    image, draw = canvas()
    width = int(0.13 * BIG)
    split = (0.62 * BIG, 0.50 * BIG)

    draw.rectangle((0.60 * BIG, 0.435 * BIG, 0.92 * BIG, 0.565 * BIG), fill=(255, 255, 255, 255))

    for arm_y, head_y in ((0.30, 0.26), (0.70, 0.74)):
        arm_end = (0.38 * BIG, arm_y * BIG)
        draw.line([split, arm_end], fill=(255, 255, 255, 255), width=width)
        draw.ellipse((split[0] - width / 2, split[1] - width / 2, split[0] + width / 2, split[1] + width / 2), fill=(255, 255, 255, 255))
        head = [(0.08, head_y), (0.40, head_y - 0.17), (0.40, head_y + 0.17)]
        draw.polygon([(x * BIG, y * BIG) for x, y in head], fill=(255, 255, 255, 255))

    return image


def save(image: Image.Image, name: str):
    folder = MOD / "quests_expanded"
    folder.mkdir(parents=True, exist_ok=True)
    image.resize((SIZE, SIZE), Image.LANCZOS).save(folder / f"icon_{name}.png")

    materials = MOD / "materials"
    materials.mkdir(parents=True, exist_ok=True)
    (materials / f"icon-quests_expanded_{name}.mat").write_text(MATERIAL.replace("{name}", name), encoding="utf-8", newline="\n")
    print(f"quests_expanded/icon_{name}.png, materials/icon-quests_expanded_{name}.mat")


def main():
    save(two_tone(arrow(pointing_right=False), WHITE), "hand_off")
    save(two_tone(arrow(pointing_right=True), WHITE), "hand_in")
    save(two_tone(chapter_arrow(), WHITE), "chapter")
    save(two_tone(branch_arrows(), WHITE), "branch")


if __name__ == "__main__":
    main()
