#!/usr/bin/env python3

import json
import math
import pathlib
import struct
import subprocess
import sys
import tempfile


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read_accessor(document: dict, binary: bytes, accessor_index: int) -> list[tuple]:
    accessor = document["accessors"][accessor_index]
    view = document["bufferViews"][accessor["bufferView"]]
    component_formats = {5125: "I", 5126: "f"}
    component_counts = {"SCALAR": 1, "VEC2": 2, "VEC3": 3}
    count = component_counts[accessor["type"]]
    item_format = "<" + component_formats[accessor["componentType"]] * count
    item_size = struct.calcsize(item_format)
    start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    return [struct.unpack_from(item_format, binary, start + index * item_size) for index in range(accessor["count"])]


def main() -> None:
    require(len(sys.argv) == 2, "validator expects the fixture-generator path")
    with tempfile.TemporaryDirectory() as directory:
        output = pathlib.Path(directory) / "writer-test.glb"
        subprocess.run([sys.argv[1], str(output)], check=True)
        data = output.read_bytes()
        orthographic_data = pathlib.Path(str(output) + ".ortho.glb").read_bytes()

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

    require(document["asset"]["generator"] == 'Drop & View "writer"\ntest', "generator JSON escaping changed")
    require(document["asset"]["extras"]["dropView"] == {
        "designCredits": 'Design: György "George" Hild\nLandscape: Example Studio',
        "schemaVersion": 1,
    }, "Drop & View asset metadata changed")
    require(document["scenes"][0]["nodes"] == [0, 1, 2, 3],
            "scene does not reference every export group and the initial camera exactly once")

    initial_view = document["scenes"][0]["extras"]["dropView"]["initialView"]
    require(initial_view["camera"] == 0, "initial-view metadata references the wrong camera")
    require(initial_view["projection"] == "perspective", "initial-view projection changed")
    require(initial_view["position"] == [10.0, 8.0, 6.0], "initial-view position changed")
    require(initial_view["target"] == [2.0, 1.0, -4.0], "initial-view target changed")
    require(initial_view["up"] == [0.0, 1.0, 0.0], "initial-view up vector changed")
    archicad_view = initial_view["archicad"]
    require(archicad_view["viewCone"] == 0.91, "Archicad view cone changed")
    require(archicad_view["rollAngle"] == 0.12, "Archicad roll angle changed")
    require(archicad_view["twoPointPerspective"] is True, "Archicad two-point flag changed")
    require(archicad_view["windowSize"] == [1600, 900], "Archicad 3D window size changed")
    require(archicad_view["zoomScale"] == [1.25, 1.5], "Archicad 3D zoom scale changed")
    require(archicad_view["zoomDisplacement"] == [12.0, -8.0], "Archicad 3D zoom displacement changed")

    require(len(document["cameras"]) == 1, "expected one startup camera")
    camera = document["cameras"][0]
    require(camera["type"] == "perspective", "startup camera type changed")
    require(camera["perspective"] == {"yfov": 0.72, "znear": 0.02},
            "startup perspective camera parameters changed")

    ortho_json_length, ortho_json_type = struct.unpack_from("<II", orthographic_data, 12)
    require(ortho_json_type == 0x4E4F534A, "orthographic fixture does not start with a JSON chunk")
    orthographic_document = json.loads(
        orthographic_data[20:20 + ortho_json_length].decode("utf-8").rstrip(" \0"))
    require(orthographic_document["asset"]["extras"]["dropView"] == {"schemaVersion": 1},
            "empty design credits produced incorrect asset metadata")
    orthographic_camera = orthographic_document["cameras"][0]
    require(orthographic_camera["type"] == "orthographic", "orthographic camera type changed")
    require(orthographic_camera["orthographic"] == {
        "xmag": 12.5,
        "ymag": 7.25,
        "znear": 0.1,
        "zfar": 250.0,
    }, "orthographic camera parameters changed")
    ortho_initial_view = orthographic_document["scenes"][0]["extras"]["dropView"]["initialView"]
    require(ortho_initial_view["projection"] == "orthographic", "orthographic initial-view metadata changed")
    require(ortho_initial_view["archicad"]["projectionMode"] == 3,
            "Archicad axonometric projection mode changed")
    require(ortho_initial_view["archicad"]["projectionMatrix"][0] == 0.5,
            "Archicad projection matrix changed")
    require(ortho_initial_view["archicad"]["inverseProjectionMatrix"][0] == 2.0,
            "Archicad inverse projection matrix changed")

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

    expected_group_names = ["Layer: Architecture", "Layer: Site", "Layer: Reused material"]
    require(len(document["nodes"]) == 4, "expected three export-group nodes and one camera node")
    require([node["name"] for node in document["nodes"][:3]] == expected_group_names,
            "export-group node names changed")
    camera_node = document["nodes"][3]
    require(camera_node["camera"] == 0, "startup camera node references the wrong camera")
    require(len(camera_node["matrix"]) == 16, "startup camera transform is not a 4x4 matrix")
    require(camera_node["matrix"][12:16] == [10.0, 8.0, 6.0, 1.0],
            "startup camera transform lost its position")
    expected_forward_unscaled = [-8.0, -7.0, -10.0]
    expected_forward_length = math.sqrt(sum(value * value for value in expected_forward_unscaled))
    expected_forward = [value / expected_forward_length for value in expected_forward_unscaled]
    camera_forward = [-camera_node["matrix"][8], -camera_node["matrix"][9], -camera_node["matrix"][10]]
    require(all(math.isclose(actual, expected, abs_tol=0.000001)
                for actual, expected in zip(camera_forward, expected_forward)),
            "startup camera does not look at its target")
    require(len(document["meshes"]) == 3, "expected three export-group meshes")
    require([mesh["name"] for mesh in document["meshes"]] == expected_group_names,
            "export-group mesh names changed")
    require([len(mesh["primitives"]) for mesh in document["meshes"]] == [4, 2, 1],
            "materials were not retained as primitives inside groups")
    require(len(accessors) == 28, "expected four accessors per primitive")
    expected_positions = [
        [(0.0, 0.0, 0.0), (2.0, 0.0, 0.0), (0.0, 3.0, 0.0)],
        [(0.0, 0.0, 1.0), (2.0, 0.0, 1.0), (0.0, 3.0, 1.0)],
        [(0.0, 0.0, 0.0), (2.0, 0.0, 0.0), (0.0, 3.0, 0.0)],
        [(0.0, 0.0, 0.0), (2.0, 0.0, 0.0), (0.0, 3.0, 0.0)],
        [(-2.0, 1.0, 4.0), (1.0, 1.0, 4.0), (-2.0, 5.0, 4.0)],
        [(-2.0, 1.0, 5.0), (1.0, 1.0, 5.0), (-2.0, 5.0, 5.0)],
        [(0.0, 0.0, 0.0), (2.0, 0.0, 0.0), (0.0, 3.0, 0.0)],
    ]
    primitives = [primitive for mesh in document["meshes"] for primitive in mesh["primitives"]]
    for primitive_index, primitive in enumerate(primitives):
        referenced = list(primitive["attributes"].values()) + [primitive["indices"]]
        require(all(0 <= index < len(accessors) for index in referenced), "primitive references an invalid accessor")
        require(primitive["material"] == [0, 1, 4, 5, 2, 3, 0][primitive_index], "primitive references the wrong material")
        require(read_accessor(document, binary, primitive["attributes"]["POSITION"]) == expected_positions[primitive_index],
                "position data changed")
        require(read_accessor(document, binary, primitive["indices"]) == [(0,), (1,), (2,)], "index data changed")

    materials = document["materials"]
    require(document["extensionsUsed"] == ["KHR_materials_transmission", "KHR_materials_ior"],
            "physical glass extensions were not declared")
    require("extensionsRequired" not in document, "optional physical-material extensions became mandatory")
    require("alphaMode" not in materials[0], "opaque textured material unexpectedly gained an alpha mode")
    require(materials[0]["pbrMetallicRoughness"]["baseColorTexture"] == {"index": 0},
            "opaque base-colour texture was not retained")
    require(materials[0]["pbrMetallicRoughness"]["baseColorFactor"] == [1.0, 1.0, 1.0, 1.0],
            "textured surface colour tinted the embedded image")
    require(materials[0]["normalTexture"] == {"index": 1}, "normal texture was not retained")
    require(materials[0]["pbrMetallicRoughness"]["metallicRoughnessTexture"] == {"index": 2},
            "packed metallic-roughness texture was not retained")
    require(materials[0]["occlusionTexture"] == {"index": 3}, "occlusion texture was not retained")
    require(materials[0]["emissiveTexture"] == {"index": 4}, "emissive texture was not retained")
    require(materials[0]["emissiveFactor"] == [0.1, 0.2, 0.3], "emissive factor was not retained")
    require(materials[1]["extensions"]["KHR_materials_transmission"]["transmissionFactor"] == 0.98,
            "clear glass lost its physical transmission preset")
    require(materials[1]["pbrMetallicRoughness"]["roughnessFactor"] == 0.03,
            "clear glass lost its roughness preset")
    require(materials[1]["pbrMetallicRoughness"]["metallicFactor"] == 0.0,
            "clear glass became metallic")
    require(materials[1]["pbrMetallicRoughness"]["baseColorFactor"][3] == 1.0,
            "clear glass uses coverage alpha")
    require(materials[1]["extensions"]["KHR_materials_ior"]["ior"] == 1.5,
            "clear glass lost its IOR")
    require("alphaMode" not in materials[1], "clear glass incorrectly uses coverage alpha")
    require(materials[1]["extras"]["archicad"]["clearGlassOverride"] is True,
            "clear glass override marker is missing")
    require("KHR_materials_volume" not in materials[1].get("extensions", {}),
            "thin glass unexpectedly gained a volume")
    require(materials[1]["extras"]["archicad"]["transparencyPercent"] == 100.0,
            "clear-glass override replaced the original Archicad transparency metadata")
    require(materials[2]["name"] == "Tinted rough glass", "material-name JSON escaping changed")
    require(materials[2]["pbrMetallicRoughness"]["baseColorFactor"] == [0.125, 0.25, 0.75, 1.0],
            "tinted-glass colour changed")
    require(materials[2]["pbrMetallicRoughness"]["roughnessFactor"] == 0.32,
            "rough glass lost its roughness")
    require(materials[2]["extensions"]["KHR_materials_transmission"]["transmissionFactor"] == 0.65,
            "tinted glass lost partial transmission")
    require("alphaMode" not in materials[2], "tinted glass incorrectly uses coverage alpha")
    require(materials[2]["extras"]["archicad"] == {
        "surfaceIndex": 13,
        "surfaceName": "Tinted rough glass",
        "materialType": 5,
        "transparencyPercent": 65.0,
        "specularPercent": 70.0,
        "shine": 1800.0,
        "emissionAttenuation": 0.0,
        "clearGlassOverride": False,
    }, "Archicad source metadata was not retained")
    require(materials[3]["alphaMode"] == "MASK", "alpha-cutout plant lost its alpha mode")
    require(materials[3]["alphaCutoff"] == 0.5, "alpha-cutout plant lost its cutoff")
    require("extensions" not in materials[3], "alpha-cutout plant was mistaken for transmission")
    require(materials[4]["alphaMode"] == "BLEND", "coverage-alpha material lost its alpha mode")
    require(materials[4]["pbrMetallicRoughness"]["baseColorFactor"] == [1.0, 1.0, 1.0, 0.5],
            "coverage-alpha material changed")
    beige = materials[5]["pbrMetallicRoughness"]
    require(materials[5]["name"] == "gipsz - szemcsés bézs", "beige surface name changed")
    require("baseColorTexture" not in beige, "untextured beige surface acquired a texture")
    require(len(beige["baseColorFactor"]) == 4, "beige base colour does not contain RGBA")
    require(all(math.isclose(actual, expected, abs_tol=0.000001) for actual, expected in zip(
        beige["baseColorFactor"], [0.799102738, 0.623960392, 0.407240212, 1.0])),
        "#E7CFAB was not exported as linear RGB with unchanged alpha")

    require(len(document["images"]) == 4, "embedded channel images were not deduplicated")
    require(len(document["textures"]) == 7, "expected one texture binding per material channel")
    require([texture["source"] for texture in document["textures"]] == [0, 1, 2, 2, 3, 0, 0],
            "texture channels do not reference the expected deduplicated images")
    require(document["samplers"][0] == {"wrapS": 10497, "wrapT": 10497}, "repeat sampler changed")
    require(document["samplers"][5] == {"wrapS": 33648, "wrapT": 10497}, "mirror-X sampler changed")
    require(document["samplers"][6] == {"wrapS": 10497, "wrapT": 33648}, "mirror-Y sampler changed")

    image_view = views[document["images"][0]["bufferView"]]
    embedded_image = binary[image_view["byteOffset"]:image_view["byteOffset"] + image_view["byteLength"]]
    require(embedded_image == b"\x89PNG\r\n\x1a\ntest", "embedded image bytes changed")


if __name__ == "__main__":
    main()
