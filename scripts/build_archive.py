import struct
from pathlib import Path


def name_hash(name, file=False):
    value = name.encode("ascii")
    dot = value.rfind(b".") if file else -1
    stem, extension = (value[:dot], value[dot:]) if dot >= 0 else (value, b"")
    low = stem[-1] | (len(stem) << 16) | (stem[0] << 24)
    if len(stem) > 2:
        low |= stem[-2] << 8
    low |= {b".kf": 0x80, b".nif": 0x8000, b".dds": 0x8080, b".wav": 0x80000000}.get(extension, 0)
    high = 0
    for part in (stem[1:-2], extension):
        result = 0
        for byte in part:
            result = (result * 0x1003F + byte) & 0xFFFFFFFF
        high = (high + result) & 0xFFFFFFFF
    return (high << 32) | low


def build(source, destination):
    folders = {}
    for path in sorted(source.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(source)
        if relative.parts[0] not in ("meshes", "textures") or path.suffix.lower() not in (".nif", ".dds"):
            raise ValueError(f"Unexpected asset: {relative}")
        folder = str(relative.parent).replace("/", "\\").lower()
        name = relative.name.lower()
        folders.setdefault(folder, {})
        if name in folders[folder]:
            raise ValueError(f"Duplicate asset: {relative}")
        folders[folder][name] = path.read_bytes()
    if not folders:
        raise ValueError("No meshes or textures to archive")
    ordered = [(folder, sorted(files.items(), key=lambda item: name_hash(item[0], file=True)))
               for folder, files in sorted(folders.items(), key=lambda item: name_hash(item[0]))]
    folder_names_size = sum(len(folder.encode("ascii")) + 1 for folder, _ in ordered)
    filenames = b"".join(name.encode("ascii") + b"\0" for _, files in ordered for name, _ in files)
    file_count = sum(len(files) for _, files in ordered)
    output = bytearray(struct.pack("<4s8I", b"BSA\0", 105, 36, 0x03,
                                   len(ordered), file_count, folder_names_size, len(filenames), 0x03))
    output += bytes(24 * len(ordered))
    records = []
    for index, (folder, files) in enumerate(ordered):
        # BSA folder offsets include the trailing filename table's length.
        struct.pack_into("<QIIQ", output, 36 + 24 * index,
                         name_hash(folder), len(files), 0, len(output) + len(filenames))
        encoded = folder.encode("ascii") + b"\0"
        output += struct.pack("<B", len(encoded)) + encoded
        for name, data in files:
            records.append((len(output), name, data))
            output += bytes(16)
    output += filenames
    for offset, name, data in records:
        struct.pack_into("<QII", output, offset, name_hash(name, file=True), len(data), len(output))
        output += data
    destination.write_bytes(output)
    print(f"Generated {destination} ({file_count} assets, {len(output)} bytes)")
