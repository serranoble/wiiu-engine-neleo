import struct, json, os, math
from PIL import Image

# Import cook_model and Vertex from mesh_cooker
import sys
sys.path.append('tools/cooker')
from mesh_cooker import Vertex, cook_model, cook_texture

glb_path = 'models/ultimate-skydome-pack/source/Ultimate skydome pack.glb'
with open(glb_path, 'rb') as f:
    f.seek(12)
    cl, ct = struct.unpack('<II', f.read(8))
    d = json.loads(f.read(cl).decode('utf-8'))
    bin_len, bin_type = struct.unpack('<II', f.read(8))
    bin_data = f.read(bin_len)

prim = d['meshes'][15]['primitives'][0]

def read_floats(accessor_idx, count_per_vertex):
    acc = d['accessors'][accessor_idx]
    bv = d['bufferViews'][acc['bufferView']]
    offset = bv.get('byteOffset', 0) + acc.get('byteOffset', 0)
    stride = bv.get('byteStride', count_per_vertex * 4)
    data = []
    fmt = '<' + 'f' * count_per_vertex
    for i in range(acc['count']):
        data.append(struct.unpack_from(fmt, bin_data, offset + i * stride))
    return data

def read_indices(accessor_idx):
    acc = d['accessors'][accessor_idx]
    bv = d['bufferViews'][acc['bufferView']]
    offset = bv.get('byteOffset', 0) + acc.get('byteOffset', 0)
    comp_type = acc['componentType']
    data = []
    if comp_type == 5123:
        for i in range(acc['count']):
            data.append(struct.unpack_from('<H', bin_data, offset + i * 2)[0])
    elif comp_type == 5125:
        for i in range(acc['count']):
            data.append(struct.unpack_from('<I', bin_data, offset + i * 4)[0])
    return data

pos_raw = read_floats(prim['attributes']['POSITION'], 3)
norm_raw = read_floats(prim['attributes']['NORMAL'], 3)
uv_raw = read_floats(prim['attributes']['TEXCOORD_0'], 2)
indices_raw = read_indices(prim['indices'])

# Center of mesh 15
cx = 350.39849853515625
cz = -1825.1580810546875

# Find max radius from center
max_r = 0.0
min_y = 1e9
max_y = -1e9
for p in pos_raw:
    x = p[0] - cx
    y = p[1]
    z = p[2] - cz
    r = math.sqrt(x*x + z*z)
    if r > max_r: max_r = r
    if y < min_y: min_y = y
    if y > max_y: max_y = y

print(f"Original Dome: Radius={max_r:.2f}m, Y_range=({min_y:.2f}m to {max_y:.2f}m)")

# Target scale: we want Radius = 90.0m so it comfortably encompasses the 70.7m ground plane
scale = 90.0 / max_r

verts = []
for i in range(len(pos_raw)):
    x = (pos_raw[i][0] - cx) * scale
    y = pos_raw[i][1] * scale
    z = (pos_raw[i][2] - cz) * scale
    
    # Inward normals for interior rendering
    nx = norm_raw[i][0]
    ny = norm_raw[i][1]
    nz = norm_raw[i][2]
    
    u = uv_raw[i][0]
    v = uv_raw[i][1]
    
    verts.append(Vertex([x, y, z], [nx, ny, nz], [u, v], (1.0, 1.0, 1.0, 1.0)))

output_wmesh = 'assets/models/skydome.wmesh'
cook_model(verts, indices_raw, output_wmesh, optimize_cache=True)
print(f"[*] Skydome model exported to {output_wmesh}")
