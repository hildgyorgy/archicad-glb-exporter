#!/usr/bin/env python3

import json
import pathlib
import struct
import subprocess
import sys
import tempfile


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    require(len(sys.argv) == 2, "validator expects the fixture-generator path")
    with tempfile.TemporaryDirectory() as directory:
        output = pathlib.Path(directory) / "writer-test.glb"
        subprocess.run([sys.argv[1], str(output)], check=True)
        data = output.read_bytes()

    require(len(data) >= 28, "GLB is shorter than its header and two chunk headers")
    magic, version, declared_length = struct.unpack_from("<III", data)
    require(magic == 0x46546C67, "invalid GLB magic")
    require(version == 2, "invalid GLB version")
    require(declared_length == len(data), "GLB header length differs from file length")

    json_length, json_type = struct.unpack_from("<II", data, 12)
    require(json_type == 0x4E4F534A, "first chunk is not JSON")
    require(json_length % 4 == 0, "JSON chunk is not four-byte aligned")
    json_start = 20
    json_end = json_start + json_length
    document = json.loads(data[json_start:json_end].decode("utf-8").rstrip(" \0"))

    binary_length, binary_type = struct.unpack_from("<II", data, json_end)
    require(binary_type == 0x004E4942, "second chunk is not binary")
    binary_start = json_end + 8
    binary = data[binary_start:binary_start + binary_length]
    require(binary_start + binary_length == len(data), "unexpected data after binary chunk")
    require(document["buffers"][0]["byteLength"] == len(binary), "buffer byteLength is incorrect")

    views = document["bufferViews"]
    for view in views:
        offset = view.get("byteOffset", 0)
        length = view["byteLength"]
        require(offset % 4 == 0, "bufferView offset is not four-byte aligned")
        require(0 <= offset <= len(binary), "bufferView starts outside the binary chunk")
        require(offset + length <= len(binary), "bufferView ends outside the binary chunk")
        if "target" in view:
            require(view["target"] in (34962, 34963), "unexpected buffer target")

    component_sizes = {5125: 4, 5126: 4}
    component_counts = {"SCALAR": 1, "VEC2": 2, "VEC3": 3}
    accessors = document["accessors"]
    for accessor in accessors:
        require(0 <= accessor["bufferView"] < len(views), "accessor references an invalid bufferView")
        required = accessor["count"] * component_sizes[accessor["componentType"]] * component_counts[accessor["type"]]
        require(required <= views[accessor["bufferView"]]["byteLength"], "accessor exceeds its bufferView")

    require(len(document["meshes"]) == 2, "expected two material-group meshes")
    require(len(accessors) == 8, "expected four accessors per mesh")
    for mesh_index, mesh in enumerate(document["meshes"]):
        primitive = mesh["primitives"][0]
        referenced = list(primitive["attributes"].values()) + [primitive["indices"]]
        require(all(0 <= index < len(accessors) for index in referenced), "primitive references an invalid accessor")
        require(primitive["material"] == mesh_index, "primitive references the wrong material")

    materials = document["materials"]
    require(materials[0]["alphaMode"] == "MASK", "masked material lost its alpha mode")
    require(materials[0]["alphaCutoff"] == 0.5, "masked material lost its cutoff")
    require(materials[1]["alphaMode"] == "BLEND", "transparent material lost its alpha mode")
    require(materials[1]["pbrMetallicRoughness"]["baseColorFactor"] == [1.0, 1.0, 1.0, 0.5],
            "textured material base colour or alpha changed")

    require(len(document["images"]) == 1, "identical embedded images were not deduplicated")
    require(len(document["textures"]) == 2, "expected one texture binding per textured material")
    require(document["textures"][0]["source"] == document["textures"][1]["source"] == 0,
            "deduplicated textures do not share their image")
    require(document["samplers"][0] == {"wrapS": 33648, "wrapT": 10497}, "mirror-X sampler changed")
    require(document["samplers"][1] == {"wrapS": 10497, "wrapT": 33648}, "mirror-Y sampler changed")

    image_view = views[document["images"][0]["bufferView"]]
    embedded_image = binary[image_view["byteOffset"]:image_view["byteOffset"] + image_view["byteLength"]]
    require(embedded_image == b"\x89PNG\r\n\x1a\ntest", "embedded image bytes changed")


if __name__ == "__main__":
    main()
