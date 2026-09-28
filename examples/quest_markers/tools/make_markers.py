"""Makes the Quest Markers mod's markers: 3D models (questmarkers/*.vmb), a picture (questmarkers/bubble.png),
and a button icon for each (questmarkers/icon_*.png, materials/icon-questmarker_*.mat).

The models are made and painted like the game's own marker (QuestGiver.vmb): 12 sides, and each part yellow at
the top, blending into orange at the bottom. The mod's shader puts the color a player picks in the yellow's
place, and shades it toward the bottom the way the game's yellow becomes orange. Pure black parts would stay
black. Run from anywhere:

    python make_markers.py [--preview <folder>]

Needs the MT2 converter's mt2model library (mt2-converter/ next to mt2-loader/) and Pillow.
"""
import argparse
import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
MOD = HERE.parent / "mod"
sys.path.insert(0, str(HERE.parents[3] / "mt2-converter"))

from mt2model import model  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

MATERIAL = "questmarker"
SEGMENTS = 12
# The game's marker colors, which the shader recognizes: lit, and shaded.
YELLOW = (0.992, 0.996, 0.047)
ORANGE = (0.784, 0.490, 0.110)


class Mesh:
    def __init__(self):
        self.vertices = []
        self.indices = []

    def vertex(self, position, normal) -> int:
        length = math.sqrt(sum(value * value for value in normal)) or 1.0
        normal = tuple(value / length for value in normal)
        self.vertices.append((*position, *YELLOW, 1.0, *normal))

        return len(self.vertices) - 1

    # Paints the part made since vertex `first` as the game paints its marker's parts: yellow at the top, blending
    # into orange at the bottom.
    def paint(self, first: int):
        heights = [vertex[1] for vertex in self.vertices[first:]]
        bottom, top = min(heights), max(heights)

        for index in range(first, len(self.vertices)):
            vertex = self.vertices[index]
            shading = (top - vertex[1]) / (top - bottom) if top > bottom else 0.0
            color = tuple(lit + (dark - lit) * shading for lit, dark in zip(YELLOW, ORANGE))
            self.vertices[index] = (*vertex[0:3], *color, 1.0, *vertex[7:10])

    # The game's triangles turn the other way from cross(b - a, c - a), so each is put the way its normals face.
    def triangle(self, a: int, b: int, c: int):
        pa, pb, pc = (self.vertices[index][0:3] for index in (a, b, c))
        facing = [sum(self.vertices[index][7 + axis] for index in (a, b, c)) for axis in range(3)]
        cross = _cross(_minus(pb, pa), _minus(pc, pa))

        if _dot(cross, facing) > 0:
            b, c = c, b

        self.indices += [a, b, c]

    def quad(self, a: int, b: int, c: int, d: int):
        self.triangle(a, b, c)
        self.triangle(a, c, d)


def _minus(a, b):
    return tuple(x - y for x, y in zip(a, b))


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def _normalized(vector):
    length = math.sqrt(_dot(vector, vector)) or 1.0

    return tuple(value / length for value in vector)


def sphere(mesh: Mesh, center, radius: float, rings: int = 6):
    rows = []

    for ring in range(rings + 1):
        polar = math.pi * ring / rings
        row = []

        for segment in range(SEGMENTS):
            around = 2 * math.pi * segment / SEGMENTS
            normal = (math.sin(polar) * math.cos(around), math.cos(polar), math.sin(polar) * math.sin(around))
            position = tuple(c + radius * n for c, n in zip(center, normal))
            row.append(mesh.vertex(position, normal))

        rows.append(row)

    for ring in range(rings):
        for segment in range(SEGMENTS):
            following = (segment + 1) % SEGMENTS
            mesh.quad(rows[ring][segment], rows[ring][following], rows[ring + 1][following], rows[ring + 1][segment])


# A shape turned around the vertical axis, from (height, radius) points, bottom to top.
def lathe(mesh: Mesh, profile):
    rows = []

    for index, (height, radius) in enumerate(profile):
        before = profile[max(index - 1, 0)]
        after = profile[min(index + 1, len(profile) - 1)]
        rise = after[0] - before[0]
        widen = after[1] - before[1]
        row = []

        for segment in range(SEGMENTS):
            around = 2 * math.pi * segment / SEGMENTS
            outward = (math.cos(around), 0.0, math.sin(around))
            normal = (outward[0] * rise, -widen, outward[2] * rise)
            row.append(mesh.vertex((radius * outward[0], height, radius * outward[2]), normal))

        rows.append(row)

    for ring in range(len(rows) - 1):
        for segment in range(SEGMENTS):
            following = (segment + 1) % SEGMENTS
            mesh.quad(rows[ring][segment], rows[ring][following], rows[ring + 1][following], rows[ring + 1][segment])


# A round tube along a line in the upright plane (x, y), with rounded ends.
def tube(mesh: Mesh, points, radius: float):
    rings = []

    for index, point in enumerate(points):
        before = points[max(index - 1, 0)]
        after = points[min(index + 1, len(points) - 1)]
        direction = _normalized((after[0] - before[0], after[1] - before[1], 0.0))
        side = (-direction[1], direction[0], 0.0)
        ring = []

        for segment in range(SEGMENTS):
            around = 2 * math.pi * segment / SEGMENTS
            normal = tuple(math.cos(around) * s + math.sin(around) * d for s, d in zip(side, (0.0, 0.0, 1.0)))
            ring.append(mesh.vertex((point[0] + radius * normal[0], point[1] + radius * normal[1], radius * normal[2]), normal))

        rings.append(ring)

    for index in range(len(rings) - 1):
        for segment in range(SEGMENTS):
            following = (segment + 1) % SEGMENTS
            mesh.quad(rings[index][segment], rings[index][following], rings[index + 1][following], rings[index + 1][segment])

    sphere(mesh, (points[0][0], points[0][1], 0.0), radius)
    sphere(mesh, (points[-1][0], points[-1][1], 0.0), radius)


# A flat outline puffed up to a ridge: every edge rises to a point in front and one behind.
def puffed(mesh: Mesh, outline, center, depth: float):
    front = (center[0], center[1], depth)
    back = (center[0], center[1], -depth)

    for index, point in enumerate(outline):
        following = outline[(index + 1) % len(outline)]

        for apex in (front, back):
            corners = [(point[0], point[1], 0.0), (following[0], following[1], 0.0), apex]
            normal = _cross(_minus(corners[1], corners[0]), _minus(corners[2], corners[0]))

            if _dot(normal, (0.0, 0.0, apex[2])) < 0:
                normal = tuple(-value for value in normal)

            a, b, c = (mesh.vertex(corner, normal) for corner in corners)
            mesh.triangle(a, b, c)


# The dot under the game's exclamation mark.
def dot(mesh: Mesh):
    lathe(mesh, [(0.0, 0.0), (0.01, 0.18), (0.06, 0.23), (0.37, 0.22), (0.42, 0.15), (0.43, 0.0)])


def question() -> Mesh:
    mesh = Mesh()
    center = (0.0, 1.5)
    radius = 0.36
    hook = [(center[0] + radius * math.cos(math.radians(angle)), center[1] + radius * math.sin(math.radians(angle)))
            for angle in range(165, -91, -15)]
    stem = [(0.0, 1.0), (0.0, 0.8), (0.0, 0.66)]
    tube(mesh, hook + stem, 0.15)
    mesh.paint(0)
    first = len(mesh.vertices)
    dot(mesh)
    mesh.paint(first)

    return mesh


def star() -> Mesh:
    mesh = Mesh()
    center = (0.0, 1.15)
    outline = []

    for point in range(10):
        angle = math.pi / 2 + point * math.pi / 5
        radius = 0.62 if point % 2 == 0 else 0.26
        outline.append((center[0] + radius * math.cos(angle), center[1] + radius * math.sin(angle)))

    puffed(mesh, outline, center, 0.2)
    mesh.paint(0)

    return mesh


def diamond() -> Mesh:
    mesh = Mesh()
    center = 1.15
    girdle = [(0.42 * math.cos(2 * math.pi * side / 6), center, 0.42 * math.sin(2 * math.pi * side / 6)) for side in range(6)]

    for tip in ((0.0, center + 0.55, 0.0), (0.0, center - 0.75, 0.0)):
        for side in range(6):
            corners = [girdle[side], girdle[(side + 1) % 6], tip]
            normal = _cross(_minus(corners[1], corners[0]), _minus(corners[2], corners[0]))
            middle = tuple(sum(values) / 3 for values in zip(*corners))

            if _dot(normal, (middle[0], middle[1] - center, middle[2])) < 0:
                normal = tuple(-value for value in normal)

            a, b, c = (mesh.vertex(corner, normal) for corner in corners)
            mesh.triangle(a, b, c)

    mesh.paint(0)

    return mesh


def write_model(mesh: Mesh, path: Path):
    fragment = model.Fragment(material=MATERIAL, format="PCN", vertices=mesh.vertices, indices=mesh.indices)
    node = model.Node(name="RootNode", lods=[[fragment]])
    path.write_bytes(model.write_model(node))


# A speech bubble: white, so it takes the picked color, with a black outline and dots that stay black.
def bubble(path: Path, size: int = 256):
    scale = 4
    image = Image.new("RGBA", (size * scale, size * scale), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    s = size * scale
    outline = 10 * scale
    box = (int(0.08 * s), int(0.1 * s), int(0.92 * s), int(0.72 * s))
    tail = [(int(0.36 * s), int(0.66 * s)), (int(0.5 * s), int(0.92 * s)), (int(0.6 * s), int(0.66 * s))]

    draw.rounded_rectangle(box, radius=int(0.16 * s), fill=(0, 0, 0, 255))
    draw.polygon([(x, y + outline // 2) for x, y in tail], fill=(0, 0, 0, 255))
    inner = (box[0] + outline, box[1] + outline, box[2] - outline, box[3] - outline)
    draw.rounded_rectangle(inner, radius=int(0.16 * s) - outline, fill=(255, 255, 255, 255))
    draw.polygon([(tail[0][0] + outline, tail[0][1] - outline), (tail[1][0], tail[1][1] - int(1.9 * outline)),
                  (tail[2][0] - outline, tail[2][1] - outline)], fill=(255, 255, 255, 255))

    for dot in (0.33, 0.5, 0.67):
        center = (int(dot * s), int(0.41 * s))
        radius = int(0.055 * s)
        draw.ellipse((center[0] - radius, center[1] - radius, center[0] + radius, center[1] + radius), fill=(0, 0, 0, 255))

    image.resize((size, size), Image.LANCZOS).save(path)


# A button icon like the game's: the shape in white and grays on transparent, 128 pixels square.
def icon(mesh: Mesh, path: Path, size: int = 128):
    scale = 4
    big = size * scale
    image = Image.new("LA", (big, big), (0, 0))
    draw = ImageDraw.Draw(image)
    triangles = [mesh.indices[index:index + 3] for index in range(0, len(mesh.indices), 3)]
    triangles.sort(key=lambda corners: sum(mesh.vertices[index][2] for index in corners))
    xs = [vertex[0] for vertex in mesh.vertices]
    ys = [vertex[1] for vertex in mesh.vertices]
    extent = max(max(xs) - min(xs), max(ys) - min(ys))
    middle = ((max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2)
    fit = big * 0.84 / extent

    for corners in triangles:
        points = [(big / 2 + (mesh.vertices[index][0] - middle[0]) * fit, big / 2 - (mesh.vertices[index][1] - middle[1]) * fit) for index in corners]
        facing = _normalized([sum(mesh.vertices[index][7 + axis] for index in corners) for axis in range(3)])
        light = max(0.0, _dot(facing, _normalized((0.3, 0.6, 0.8))))
        draw.polygon(points, fill=(int(255 * (0.55 + 0.45 * light)), 255))

    image.resize((size, size), Image.LANCZOS).save(path)


def picture_icon(picture: Path, path: Path, size: int = 128):
    image = Image.open(picture).convert("RGBA").resize((size, size), Image.LANCZOS)
    gray = image.convert("L")
    Image.merge("LA", (gray, image.getchannel("A"))).save(path)


ICON_MATERIAL = """Material{
	name "icon-questmarker_{name}"
	mode normal
	color 1.0 1.0 1.0 1.0
	culling none
	zread false
	zwrite false
	glow false
	texture "questmarkers/icon_{name}.png" clampUV
}
"""


def write_icon_material(name: str):
    (MOD / "materials" / f"icon-questmarker_{name}.mat").write_text(ICON_MATERIAL.replace("{name}", name), encoding="utf-8", newline="\n")


# The tab's own buttons, drawn like the game's icons: white lines on transparent.
def button_icons(folder: Path, size: int = 128):
    scale = 4
    big = size * scale
    line = 9 * scale
    white = (255, 255)

    def canvas():
        image = Image.new("LA", (big, big), (0, 0))

        return image, ImageDraw.Draw(image)

    def save(image, name: str):
        image.resize((size, size), Image.LANCZOS).save(folder / f"icon_{name}.png")
        write_icon_material(name)

    image, draw = canvas()
    draw.rounded_rectangle((0.34 * big, 0.14 * big, 0.84 * big, 0.7 * big), radius=0.07 * big, outline=white, width=line)
    draw.rounded_rectangle((0.16 * big, 0.3 * big, 0.66 * big, 0.86 * big), radius=0.07 * big, fill=(0, 0), outline=white, width=line)
    save(image, "copy")

    image, draw = canvas()
    draw.rounded_rectangle((0.2 * big, 0.18 * big, 0.8 * big, 0.88 * big), radius=0.07 * big, outline=white, width=line)
    draw.rounded_rectangle((0.36 * big, 0.1 * big, 0.64 * big, 0.26 * big), radius=0.04 * big, fill=white)

    for row in (0.44, 0.58, 0.72):
        draw.line((0.32 * big, row * big, 0.68 * big, row * big), fill=white, width=line)

    save(image, "paste")

    image, draw = canvas()
    box = (0.2 * big, 0.2 * big, 0.8 * big, 0.8 * big)
    draw.arc(box, start=-60, end=210, fill=white, width=line)
    tip = (0.5 * big + 0.3 * big * math.cos(math.radians(-60)), 0.5 * big + 0.3 * big * math.sin(math.radians(-60)))
    draw.polygon([(tip[0] - 0.13 * big, tip[1] - 0.03 * big), (tip[0] + 0.1 * big, tip[1] - 0.12 * big), (tip[0] + 0.05 * big, tip[1] + 0.13 * big)], fill=white)
    save(image, "reset")


def preview(mesh: Mesh, path: Path, size: int = 200):
    """A quick picture with the game's up and right, colored as the game draws it by default, for checking a shape by eye."""
    image = Image.new("RGB", (size, size), (40, 40, 60))
    draw = ImageDraw.Draw(image)
    triangles = [mesh.indices[index:index + 3] for index in range(0, len(mesh.indices), 3)]
    triangles.sort(key=lambda corners: sum(mesh.vertices[index][2] for index in corners))

    for corners in triangles:
        points = [(size / 2 + mesh.vertices[index][0] * size * 0.4, size * 0.95 - mesh.vertices[index][1] * size * 0.44) for index in corners]
        color = [sum(mesh.vertices[index][3 + channel] for index in corners) / 3 for channel in range(3)]
        draw.polygon(points, fill=tuple(int(255 * channel) for channel in color))

    image.save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--preview", type=Path, help="also draw each model into this folder")
    arguments = parser.parse_args()

    folder = MOD / "questmarkers"
    folder.mkdir(parents=True, exist_ok=True)
    shapes = {"question": question(), "star": star(), "diamond": diamond()}

    for name, mesh in shapes.items():
        write_model(mesh, folder / f"{name}.vmb")
        icon(mesh, folder / f"icon_{name}.png")
        write_icon_material(name)
        print(f"questmarkers/{name}.vmb: {len(mesh.vertices)} vertices, {len(mesh.indices) // 3} triangles")

        if arguments.preview:
            arguments.preview.mkdir(parents=True, exist_ok=True)
            preview(mesh, arguments.preview / f"{name}.png")

    bubble(folder / "bubble.png")
    picture_icon(folder / "bubble.png", folder / "icon_bubble.png")
    write_icon_material("bubble")
    print("questmarkers/bubble.png")
    button_icons(folder)
    print("questmarkers/icon_copy.png, icon_paste.png, icon_reset.png")


if __name__ == "__main__":
    main()
