import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def subrecord(tag, payload):
    return struct.pack("<4sH", tag.encode(), len(payload)) + payload


def text(tag, value):
    return subrecord(tag, value.encode("utf-8") + b"\0")


def record(tag, form_id, payload, flags=0):
    return struct.pack("<4sIIIIHH", tag.encode(), len(payload), flags, form_id, 0, 44, 0) + payload


def group(tag, payload):
    return struct.pack("<4sI4sIII", b"GRUP", len(payload) + 24, tag.encode(), 0, 0, 0) + payload


def route_records(index):
    effect_id = 0x01000800 + index * 3
    spell_id = effect_id + 1
    hazard_id = effect_id + 2
    suffix = "" if index == 0 else "B"
    effect = bytearray(152)
    for offset, value in {
        0: 0x8C10, 8: hazard_id, 12: 0xFFFFFFFF, 16: 0xFFFFFFFF,
        64: 25, 68: 0xFFFFFFFF, 80: 1, 88: 0xFFFFFFFF, 140: 2,
    }.items():
        struct.pack_into("<I", effect, offset, value)
    struct.pack_into("<f", effect, 112, 1.0)
    effect_record = record("MGEF", effect_id,
        text("EDID", "AQTGuideEffect" + suffix) + text("FULL", "Active Quest Trail")
        + subrecord("DATA", effect) + text("DNAM", ""))

    spell_data = struct.pack("<III f II f II", 0, 1, 0, 0.0, 1, 0, 0.0, 0, 0)
    spell_record = record("SPEL", spell_id,
        text("EDID", "AQTGuideSpell" + suffix) + subrecord("OBND", bytes(12))
        + text("FULL", "Active Quest Trail") + text("DESC", "")
        + subrecord("SPIT", spell_data) + subrecord("EFID", struct.pack("<I", effect_id))
        + subrecord("EFIT", struct.pack("<fII", 0.0, 0, 8)))

    hazard_data = struct.pack("<IffffIIIII", 1, 0.0, 8.0, 0.0, 10.0, 6, 0, 0, 0, 0)
    hazard_record = record("HAZD", hazard_id,
        text("EDID", "AQTGuideHazard" + suffix) + subrecord("OBND", struct.pack("<hhhhhh", -14, -72, -14, 14, 72, 14))
        + text("MODL", "ActiveQuestTrail\\TrailWisp.nif") + subrecord("DATA", hazard_data))
    return effect_record, spell_record, hazard_record


def destination_shader():
    data = bytearray(400)
    for offset, value in {
        0x04: 5, 0x08: 1, 0x0C: 4, 0x10: 0xFFFFFFFF, 0x38: 0xFFFFFFFF,
        0x5C: 2, 0x60: 5, 0x64: 1, 0x68: 4, 0x6C: 2,
        0x114: 1, 0x118: 1, 0x138: 0xFFFFFFFF, 0x13C: 0xFFFFFFFF, 0x180: 8,
    }.items():
        struct.pack_into("<I", data, offset, value)
    for offset, value in {
        0x14: 0.5, 0x1C: 0.5, 0x20: 1.0, 0x2C: 0.05, 0x30: 0.12,
        0x34: 2.0, 0x3C: 0.5, 0x44: 0.5, 0x48: 1.0, 0x54: 0.2,
        0x58: 0.7, 0x84: 1.0, 0xAC: 1.0, 0xB0: 1.0,
        0x140: 1.0, 0x144: 1.0, 0x148: 1.0, 0x150: 0.5, 0x154: 1.0,
        0x158: 2.0, 0x184: 1.0, 0x188: 1.0,
    }.items():
        struct.pack_into("<f", data, offset, value)
    return record("EFSH", 0x01000806, text("EDID", "AQTDoorGlow")
        + text("ICON", "Effects\\MagicCaustic01.dds") + subrecord("DATA", data))


def build(destination=ROOT / "package"):
    header = subrecord("HEDR", struct.pack("<fII", 1.7, 11, 0x807))
    header += text("CNAM", "Active Quest Trail")
    header += text("SNAM", "Invisible guide probes with persistent floating wisps")
    header += text("MAST", "Skyrim.esm") + subrecord("DATA", bytes(8))
    routes = [route_records(i) for i in range(2)]

    output = destination / "ActiveQuestTrail.esp"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(record("TES4", 0, header, 0x200)
        + b"".join(group(tag, b"".join(route[i] for route in routes))
                   for i, tag in enumerate(["MGEF", "SPEL", "HAZD"]))
        + group("EFSH", destination_shader()))
    print(f"Generated {output} ({output.stat().st_size} bytes)")


if __name__ == "__main__":
    build()
