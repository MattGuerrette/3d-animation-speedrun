import struct
import json

def dump_accessor_to_f(gltf, accessor_idx, output, expected_component_type, expected_type):
    position_attribute = gltf["accessors"][accessor_idx]
    assert position_attribute["componentType"] == expected_component_type
    assert position_attribute["type"] == expected_type

    position_buf_view_idx = position_attribute["bufferView"]
    buffer_view = gltf["bufferViews"][position_buf_view_idx]
    offs = buffer_view["byteOffset"]
    length = buffer_view["byteLength"]
    buf_idx = buffer_view["buffer"]

    with open(gltf["buffers"][buf_idx]["uri"], "rb") as in_file:
        in_file.seek(offs, 0)
        output.write(in_file.read(length))

def dump_accessor(gltf, accessor_idx, path, expected_component_type, expected_type):
    with open(path, "wb") as output:
        dump_accessor_to_f(gltf, accessor_idx, output, expected_component_type, expected_type)

def animationTypeVal(s):
    if s == "translation":
        return 0
    if s == "rotation":
        return 1
    if s == "scale":
        return 2

def main():
    with open("untitled.gltf", "r") as f:
        gltf = json.load(f)


    meshes = gltf["meshes"]
    assert len(meshes) == 1

    mesh = gltf["meshes"][0]
    primitives = mesh["primitives"]
    assert len(primitives) == 1

    attributes = primitives[0]["attributes"]

    dump_accessor(gltf, attributes["POSITION"], "positions.bin", 5126, "VEC3")
    dump_accessor(gltf, attributes["NORMAL"], "normals.bin", 5126, "VEC3")
    dump_accessor(gltf, primitives[0]["indices"], "indices.bin", 5123, "SCALAR")
    dump_accessor(gltf, attributes["JOINTS_0"], "vert_joints.bin", 5121, "VEC4")
    dump_accessor(gltf, attributes["WEIGHTS_0"], "vert_weights.bin", 5126, "VEC4")

    nodes = gltf["nodes"]

    parents = [0xffffffff for _ in nodes]
    for i, node in enumerate(nodes):
        for child in node.get("children", []):
            parents[child] = i

    with open("nodes.bin", "wb") as f:
        for i, node in enumerate(nodes):
            translation = node.get("translation", [0, 0, 0])
            f.write(struct.pack('<' + 'f'*len(translation), *translation))
            rotation = node.get("rotation", [0, 0, 0, 1])
            f.write(struct.pack('<' + 'f'*len(rotation), *rotation))
            scale = node.get("scale", [1, 1, 1])
            f.write(struct.pack('<' + 'f'*len(scale), *scale))
            parent = parents[i]
            f.write(struct.pack('<I', parent))

    animations = gltf["animations"]
    assert len(animations) == 1;

    animation = animations[0]
    for i, channel in enumerate(animation["channels"]):
        with open("animations_{}.bin".format(i), "wb") as f:
            f.write(struct.pack('<I', channel["target"]["node"]))
            f.write(struct.pack('<I', animationTypeVal(channel["target"]["path"])))

            sampler_idx = channel["sampler"]
            samplers = animation["samplers"]

            times_accessor = samplers[sampler_idx]["input"]
            data_accessor = samplers[sampler_idx]["output"]
            f.write(struct.pack('<I', gltf["accessors"][times_accessor]["count"]))
            dump_accessor_to_f(gltf, times_accessor, f, 5126, "SCALAR")

            expected_type = "VEC4" if channel["target"]["path"] == "rotation" else "VEC3"
            dump_accessor_to_f(gltf, data_accessor, f, 5126, expected_type)



if __name__ == '__main__':
    main()
