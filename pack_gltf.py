import json

def dump_accessor(gltf, accessor_idx, path, expected_component_type, expected_type):
    position_attribute = gltf["accessors"][accessor_idx]
    assert position_attribute["componentType"] == expected_component_type
    assert position_attribute["type"] == expected_type

    position_buf_view_idx = position_attribute["bufferView"]
    buffer_view = gltf["bufferViews"][position_buf_view_idx]
    offs = buffer_view["byteOffset"]
    length = buffer_view["byteLength"]
    buf_idx = buffer_view["buffer"]

    with open(gltf["buffers"][buf_idx]["uri"], "rb") as in_file, \
         open(path, "wb") as output:
        in_file.seek(offs, 0)
        output.write(in_file.read(length))

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


if __name__ == '__main__':
    main()
