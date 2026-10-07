import math
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TEXTURE = r"textures\ActiveQuestTrail\wisp.dds"


def pack(fmt, *values):
    return struct.pack("<" + fmt, *values)


def string(value):
    encoded = value.encode("ascii")
    return pack("I", len(encoded)) + encoded


def object_net(name):
    return pack("iIi", name, 0, -1)


def scene_object(name, hidden=False):
    return (object_net(name) + pack("I", 0x8000E | int(hidden))
            + pack("3f", 0, 0, 0) + pack("9f", 1, 0, 0, 0, 1, 0, 0, 0, 1)
            + pack("fi", 1, -1))


def shape(name, shader, alpha, spark=False):
    width, length = (3, 3) if spark else (14, 72)
    vertices = [(-width, -length, 0, 0, 1), (width, -length, 0, 1, 1),
                (width, length, 0, 1, 0), (-width, length, 0, 0, 0),
                (0, -length, -width, 0, 1), (0, -length, width, 1, 1),
                (0, length, width, 1, 0), (0, length, -width, 0, 0)]
    if spark:
        vertices += [(-3, 0, -3, 0, 1), (3, 0, -3, 1, 1),
                     (3, 0, 3, 1, 0), (-3, 0, 3, 0, 0)]
    bases = [((128, 128, 255), (255, 128, 128)),
             ((0, 128, 128), (128, 128, 255)),
             ((128, 0, 128), (255, 128, 128))]
    vertex_data = b"".join(
        pack("4f2e8B", x, y, z, 0, u, v, *bases[i // 4][0], 255, *bases[i // 4][1], 128)
        for i, (x, y, z, u, v) in enumerate(vertices))
    triangles = b"".join(pack("6H", i, i + 1, i + 2, i, i + 2, i + 3)
                         for i in range(0, len(vertices), 4))
    vertex_desc = (0x41B << 44) | (24 << 18) | (20 << 14) | (16 << 6) | 7
    return (scene_object(name, hidden=True) + pack("4f", 0, 0, 0, math.hypot(width, length))
            + pack("3iQHHI", -1, shader, alpha, vertex_desc, len(vertices) // 2,
                   len(vertices), len(vertex_data) + len(triangles))
            + vertex_data + triangles + pack("I", 0))


def effect_material(name, texture):
    return (object_net(name) + pack("II4f", 0x80000000, 0x40018, 0, 0, 1, 1)
            + string(texture) + pack("I10f", 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0) + string(""))


def mesh():
    names = ["AQT_Trail", "AQT_Wisp", "AQT_Material", "AQT_Spark0", "AQT_Spark1"]
    types = ["NiNode", "BSTriShape", "BSEffectShaderProperty", "NiAlphaProperty"]
    blocks = [scene_object(0, hidden=True) + pack("I3iI", 3, 1, 4, 7, 0),
              shape(1, 2, 3), effect_material(2, TEXTURE), object_net(-1) + pack("HB", 4109, 0)]
    for i in range(2):
        index = len(blocks)
        blocks += [shape(3 + i, index + 1, index + 2, spark=True),
                   effect_material(2, r"textures\ActiveQuestTrail\spark.dds"),
                   object_net(-1) + pack("HB", 4109, 0)]
    header = b"Gamebryo File Format, Version 20.2.0.7\n" + pack("IBIII", 0x14020007, 1, 12, len(blocks), 100)
    header += b"\x01\0" * 3
    header += pack("H", len(types)) + b"".join(string(name) for name in types)
    header += pack("10H", 0, 1, 2, 3, 1, 2, 3, 1, 2, 3)
    header += pack("10I", *(len(block) for block in blocks))
    header += pack("II", len(names), max(map(len, names))) + b"".join(string(name) for name in names)
    header += pack("I", 0)
    return header + b"".join(blocks) + pack("Ii", 1, 0)


def mist_mesh():
    names = ["AQT_Mist", "AQT_Wisp", "AQT_Material", "AQT_Spark0", "AQT_Spark1",
             "AQT_Emitter", "AQT_Age", "AQT_Colour", "AQT_Rotation", "AQT_Position", "AQT_Bounds", "AQT_Scale"]

    def controller(next_controller, flags=72):
        return pack("iH4fi", next_controller, flags, 1, 0, 0, 1000000, 1)

    def modifier(name, order):
        return pack("iIiB", name, order, 1, 1)

    particle = bytearray(scene_object(1, hidden=True))
    struct.pack_into("<i", particle, 8, 11)
    particle += pack("4f3i8B4HiBI", 0, 0, 0, 88, -1, 2, 3,
                     81, 0, 0, 4, 0, 32, 64, 8, 16000, 14000, 128, 64, 10, 0, 7)
    particle += pack("7i", 15, 16, 17, 18, 22, 19, 20)
    shader = (object_net(2) + pack("II4f", 0xC0000000, 0x40038, 0, 0, 1, 1)
              + string(r"textures\effects\MagicCaustic01.dds")
              + pack("I10f", 3, 1, 1, 0, 0, 1, 1, 1, 0, 0, 8) + string(""))
    blocks = [scene_object(0, hidden=True) + pack("I4iI", 4, 1, 4, 7, 21, 0),
              bytes(particle), shader, object_net(-1) + pack("HB", 4109, 0)]
    for i in range(2):
        index = len(blocks)
        blocks += [shape(3 + i, index + 1, index + 2, spark=True),
                   effect_material(2, r"textures\ActiveQuestTrail\spark.dds"),
                   object_net(-1) + pack("HB", 4109, 0)]
    data = pack("IHBBBHIB4fBHi", 0, 24, 0, 0, 1, 0, 0, 0, 0, 0, 0, 88, 1, 0, -1)
    data += pack("BH5BI", 1, 0, 1, 0, 1, 0, 1, 4)
    data += b"".join(pack("4f", u, 0.5, v, 0.5) for u, v in [(0, 0), (0.5, 0), (0, 0.5), (0.5, 0.5)])
    data += pack("fH3fB", 1, 0, 0, 0, 0, 1)
    blocks += [data,
               controller(12) + pack("3i", 13, 5, 14),
               controller(-1, 76),
               pack("fi", 24, -1), pack("Bi", 1, -1),
               modifier(6, 0) + pack("Bi", 0, -1),
               modifier(5, 1000) + pack("14fi3f", 12, 2, math.pi / 2, 0.12, math.pi / 2, 0.18,
                                        1, 1, 1, 1, 12, 2, 0.6, 0.05, 21, 6, 88, 6),
               modifier(7, 3000) + pack("18f", 0.2, 0.75, 0, 0.3, 0.7, 1,
                                        1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0),
               modifier(8, 3000) + pack("4fBB3f", 0.1, 0.05, -math.pi, math.pi, 1, 1, 1, 0, 0),
               modifier(9, 6000), modifier(10, 7000) + pack("H", 0),
               scene_object(5) + pack("II", 0, 0),
               modifier(11, 3000) + pack("I2f", 2, 0.9, 1)]
    block_types = ["NiNode", "NiParticleSystem", "BSEffectShaderProperty", "NiAlphaProperty",
                   "BSTriShape", "BSEffectShaderProperty", "NiAlphaProperty",
                   "BSTriShape", "BSEffectShaderProperty", "NiAlphaProperty", "NiPSysData",
                   "NiPSysEmitterCtlr", "NiPSysUpdateCtlr", "NiFloatInterpolator", "NiBoolInterpolator",
                   "NiPSysAgeDeathModifier", "NiPSysBoxEmitter", "BSPSysSimpleColorModifier",
                   "NiPSysRotationModifier", "NiPSysPositionModifier", "NiPSysBoundUpdateModifier",
                   "NiNode", "BSPSysScaleModifier"]
    types = list(dict.fromkeys(block_types))
    header = b"Gamebryo File Format, Version 20.2.0.7\n" + pack("IBIII", 0x14020007, 1, 12, len(blocks), 100)
    header += b"\x01\0" * 3
    header += pack("H", len(types)) + b"".join(string(name) for name in types)
    header += pack(f"{len(blocks)}H", *(types.index(name) for name in block_types))
    header += pack(f"{len(blocks)}I", *(len(block) for block in blocks))
    header += pack("II", len(names), max(map(len, names))) + b"".join(string(name) for name in names)
    header += pack("I", 0)
    return header + b"".join(blocks) + pack("Ii", 1, 0)


def texture(size=256, spark=False):
    pixels = []
    for y in range(size):
        for x in range(size):
            u, v = (x + 0.5) / size, (y + 0.5) / size
            if spark:
                radius = math.hypot(u - 0.5, v - 0.5)
                core = math.exp(-0.5 * (radius / 0.07) ** 2)
                halo = 0.2 * math.exp(-0.5 * (radius / 0.2) ** 2)
                rays = 0.25 * math.exp(-min(abs(u - 0.5), abs(v - 0.5)) * 100) * max(0, 1 - radius * 2)
                pixels.append(round(min(1, (core + halo + rays) * max(0, 1 - (radius * 2) ** 4)) * 255))
                continue
            taper = math.sin(math.pi * v) ** 0.7
            centre = 0.5 + 0.1 * math.sin(v * math.tau)
            halo = 0.18 * math.exp(-0.5 * ((u - centre) / 0.16) ** 2)
            core = 0.8 * math.exp(-0.5 * ((u - centre) / 0.025) ** 2)
            strands = sum(0.35 * math.exp(-0.5 * ((u - centre - offset
                          * math.sin(math.pi * v)) / 0.012) ** 2)
                          for offset in (-0.18, -0.1, 0.12, 0.2))
            edge = min(1.0, u * 16, (1 - u) * 16)
            pixels.append(round(min(1, (halo + core + strands) * taper * edge) * 255))
    levels = []
    width = size
    while True:
        levels.append(b"".join(bytes((255, 255, 255, alpha)) for alpha in pixels))
        if width == 1:
            break
        pixels = [round(sum(pixels[(y * 2 + dy) * width + x * 2 + dx]
                            for dy in range(2) for dx in range(2)) / 4)
                  for y in range(width // 2) for x in range(width // 2)]
        width //= 2
    header = pack("7I", 124, 0x2100F, size, size, size * 4, 0, len(levels)) + bytes(44)
    header += pack("8I", 32, 0x41, 0, 32, 0xFF, 0xFF00, 0xFF0000, 0xFF000000)
    header += pack("5I", 0x401008, 0, 0, 0, 0)
    return b"DDS " + header + b"".join(levels)


def build(destination=ROOT / "package"):
    for relative, data in [("meshes/ActiveQuestTrail/TrailWisp.nif", mesh()),
                           ("meshes/ActiveQuestTrail/TrailMist.nif", mist_mesh()),
                           ("textures/ActiveQuestTrail/wisp.dds", texture()),
                           ("textures/ActiveQuestTrail/spark.dds", texture(64, spark=True))]:
        output = destination / relative
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(data)
        print(f"Generated {output} ({len(data)} bytes)")


if __name__ == "__main__":
    build()
