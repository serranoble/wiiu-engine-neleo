"""
tools/cooker/fbx_parser.py

Pure-Python FBX 7.x Binary Parser for Wii U Mesh Cooker.
Extracts static 3D meshes, normals, UVs, and materials without third-party dependencies.
Complies with Jason Gregory's Game Engine Architecture (3rd Ed., Cap. 12 & 13).
"""

import struct
import zlib
import math
from typing import List, Tuple, Dict, Optional, Any

class FBXNode:
    __slots__ = ('name', 'props', 'children')
    def __init__(self, name: str, props: list, children: list):
        self.name = name
        self.props = props
        self.children = children

    def find_child(self, name: str) -> Optional['FBXNode']:
        for c in self.children:
            if c.name == name:
                return c
        return None

    def find_children(self, name: str) -> List['FBXNode']:
        return [c for c in self.children if c.name == name]

def read_fbx_binary_nodes(filepath: str) -> List[FBXNode]:
    with open(filepath, 'rb') as f:
        data = f.read()

    magic = b'Kaydara FBX Binary  \x00'
    if not data.startswith(magic[:20]):
        raise ValueError(f"'{filepath}' is not a valid FBX Binary file.")

    version = struct.unpack('<I', data[23:27])[0]
    is_64bit = (version >= 7500)
    offset = 27

    def read_node(off: int) -> Tuple[Optional[FBXNode], int]:
        if off >= len(data):
            return None, off

        if is_64bit:
            if off + 25 > len(data):
                return None, len(data)
            end_off, num_props, prop_len = struct.unpack('<QQQ', data[off:off+24])
            off += 24
        else:
            if off + 13 > len(data):
                return None, len(data)
            end_off, num_props, prop_len = struct.unpack('<III', data[off:off+12])
            off += 12

        name_len = data[off]
        off += 1
        name = data[off:off+name_len].decode('ascii', errors='ignore')
        off += name_len

        # Null sentinel record marks end of child list
        if end_off == 0 and name_len == 0:
            return None, off

        props = []
        for _ in range(num_props):
            ptype = chr(data[off])
            off += 1
            if ptype == 'Y':
                props.append(struct.unpack('<h', data[off:off+2])[0])
                off += 2
            elif ptype == 'C':
                props.append(bool(data[off]))
                off += 1
            elif ptype == 'I':
                props.append(struct.unpack('<i', data[off:off+4])[0])
                off += 4
            elif ptype == 'F':
                props.append(struct.unpack('<f', data[off:off+4])[0])
                off += 4
            elif ptype == 'D':
                props.append(struct.unpack('<d', data[off:off+8])[0])
                off += 8
            elif ptype == 'L':
                props.append(struct.unpack('<q', data[off:off+8])[0])
                off += 8
            elif ptype in ('f', 'd', 'l', 'i', 'b'):
                arr_len, encoding, comp_len = struct.unpack('<III', data[off:off+12])
                off += 12
                raw = data[off:off+comp_len]
                off += comp_len
                if encoding == 1:
                    raw = zlib.decompress(raw)
                fmt_map = {
                    'f': f'<{arr_len}f',
                    'd': f'<{arr_len}d',
                    'l': f'<{arr_len}q',
                    'i': f'<{arr_len}i',
                    'b': f'<{arr_len}B'
                }
                props.append(struct.unpack(fmt_map[ptype], raw))
            elif ptype == 'S':
                slen = struct.unpack('<I', data[off:off+4])[0]
                off += 4
                props.append(data[off:off+slen].decode('utf-8', errors='ignore'))
                off += slen
            elif ptype == 'R':
                slen = struct.unpack('<I', data[off:off+4])[0]
                off += 4
                props.append(data[off:off+slen])
                off += slen
            else:
                raise ValueError(f"Unknown property type '{ptype}' at offset {off}")

        children = []
        while off < end_off:
            child, off = read_node(off)
            if child is None:
                break
            children.append(child)

        return FBXNode(name, props, children), end_off

    top_nodes = []
    footer_len = 160 if is_64bit else 128
    while offset < len(data) - footer_len:
        node, offset = read_node(offset)
        if node is None:
            break
        top_nodes.append(node)

    return top_nodes

def extract_fbx_meshes(filepath: str, default_color=(1.0, 1.0, 1.0, 1.0)) -> Dict[str, Dict[str, Any]]:
    """
    Parses an FBX file and extracts all geometries with positions, normals, and UVs.
    Returns a dictionary of mesh data keyed by simplified clean name (e.g. 'house', 'props', 'alpha').
    """
    nodes = read_fbx_binary_nodes(filepath)
    
    objects_node = None
    connections_node = None
    for n in nodes:
        if n.name == 'Objects':
            objects_node = n
        elif n.name == 'Connections':
            connections_node = n

    if not objects_node:
        raise ValueError("FBX has no 'Objects' section.")

    # Parse connections
    # C: "OO", source_id, target_id
    geom_to_model: Dict[int, int] = {}
    model_to_material: Dict[int, int] = {}
    if connections_node:
        for c in connections_node.children:
            if c.name == 'C' and len(c.props) >= 3:
                ctype = c.props[0]
                src = c.props[1]
                dst = c.props[2]
                if ctype == 'OO':
                    geom_to_model[src] = dst

    # Parse Models for names and rotation
    models_info: Dict[int, Dict[str, Any]] = {}
    for obj in objects_node.children:
        if obj.name == 'Model':
            mid = obj.props[0]
            raw_name = obj.props[1].split('\x00')[0]
            rot = [0.0, 0.0, 0.0]
            scale = [1.0, 1.0, 1.0]
            props70 = obj.find_child('Properties70')
            if props70:
                for p in props70.children:
                    if p.name == 'P' and len(p.props) >= 7:
                        prop_name = p.props[0]
                        if prop_name == 'Lcl Rotation':
                            rot = [float(p.props[4]), float(p.props[5]), float(p.props[6])]
                        elif prop_name == 'Lcl Scaling':
                            scale = [float(p.props[4]), float(p.props[5]), float(p.props[6])]
            models_info[mid] = {'name': raw_name, 'rotation': rot, 'scaling': scale}

    # Parse Geometries
    extracted_meshes: Dict[str, Dict[str, Any]] = {}

    for obj in objects_node.children:
        if obj.name != 'Geometry':
            continue

        gid = obj.props[0]
        raw_gname = obj.props[1].split('\x00')[0]
        model_id = geom_to_model.get(gid)
        model_meta = models_info.get(model_id, {'name': raw_gname, 'rotation': [-90.0, 0.0, 0.0], 'scaling': [1.0, 1.0, 1.0]})
        mname = model_meta['name']

        # Determine clean name
        clean_name = mname.lower().replace('soloport_', '').replace('_highpoly', '').replace('_high_poly', '')
        if not clean_name:
            clean_name = raw_gname.lower().replace('cube.', 'mesh_')

        verts_node = obj.find_child('Vertices')
        pvi_node = obj.find_child('PolygonVertexIndex')
        if not verts_node or not pvi_node:
            continue

        raw_verts = verts_node.props[0]
        pvi = pvi_node.props[0]

        # Normals
        norm_node = obj.find_child('LayerElementNormal')
        raw_normals = None
        if norm_node:
            norm_arr_node = norm_node.find_child('Normals')
            if norm_arr_node and norm_arr_node.props:
                raw_normals = norm_arr_node.props[0]

        # UVs
        uv_node = obj.find_child('LayerElementUV')
        raw_uvs = None
        uv_indices = None
        if uv_node:
            uv_arr_node = uv_node.find_child('UV')
            uvi_arr_node = uv_node.find_child('UVIndex')
            if uv_arr_node and uv_arr_node.props:
                raw_uvs = uv_arr_node.props[0]
            if uvi_arr_node and uvi_arr_node.props:
                uv_indices = uvi_arr_node.props[0]

        # Check if rotation -90 around X is needed (converting Z-up to Y-up)
        apply_rot_x90 = True
        rot_x = model_meta['rotation'][0]
        if abs(rot_x - (-90.0)) < 1.0 or abs(rot_x - 270.0) < 1.0:
            apply_rot_x90 = True

        # Process Polygons into Triangles
        triangles: List[List[Tuple[Tuple[float, float, float], Tuple[float, float, float], Tuple[float, float]]]] = []
        curr_poly: List[Tuple[Tuple[float, float, float], Tuple[float, float, float], Tuple[float, float]]] = []

        for k, idx in enumerate(pvi):
            is_end = (idx < 0)
            v_idx = (~idx) if is_end else idx

            # Position
            vx = raw_verts[v_idx * 3]
            vy = raw_verts[v_idx * 3 + 1]
            vz = raw_verts[v_idx * 3 + 2]

            if apply_rot_x90:
                pos = (vx, vz, -vy)
            else:
                pos = (vx, vy, vz)

            # Normal
            if raw_normals and k * 3 + 2 < len(raw_normals):
                nx = raw_normals[k * 3]
                ny = raw_normals[k * 3 + 1]
                nz = raw_normals[k * 3 + 2]
                if apply_rot_x90:
                    norm = (nx, nz, -ny)
                else:
                    norm = (nx, ny, nz)
            else:
                norm = (0.0, 1.0, 0.0)

            # UV (Convert Blender bottom-left V origin to GX2 top-left V = 1.0 - V)
            if raw_uvs:
                if uv_indices and k < len(uv_indices):
                    u_idx = uv_indices[k]
                    u = raw_uvs[u_idx * 2]
                    v = 1.0 - raw_uvs[u_idx * 2 + 1]
                elif k * 2 + 1 < len(raw_uvs):
                    u = raw_uvs[k * 2]
                    v = 1.0 - raw_uvs[k * 2 + 1]
                else:
                    u, v = 0.0, 0.0
                uv = (u, v)
            else:
                uv = (0.0, 0.0)

            curr_poly.append((pos, norm, uv))

            if is_end:
                # Fan triangulation
                for i in range(1, len(curr_poly) - 1):
                    triangles.append([curr_poly[0], curr_poly[i], curr_poly[i + 1]])
                curr_poly = []

        # Deduplicate vertices for Post-Transform Vertex Cache (PTVC) efficiency
        unique_map: Dict[Tuple, int] = {}
        final_vertices: List[Dict[str, Any]] = []
        final_indices: List[int] = []
        submesh_col = (1.0, 1.0, 1.0, 0.55) if 'alpha' in clean_name else default_color

        for tri in triangles:
            for corner in tri:
                pos, norm, uv = corner
                key = (
                    round(pos[0], 5), round(pos[1], 5), round(pos[2], 5),
                    round(norm[0], 4), round(norm[1], 4), round(norm[2], 4),
                    round(uv[0], 4), round(uv[1], 4)
                )
                if key not in unique_map:
                    idx = len(final_vertices)
                    unique_map[key] = idx
                    final_vertices.append({
                        'pos': list(pos),
                        'norm': list(norm),
                        'uv': list(uv),
                        'col': list(submesh_col)
                    })
                final_indices.append(unique_map[key])

        extracted_meshes[clean_name] = {
            'model_name': mname,
            'geom_name': raw_gname,
            'vertices': final_vertices,
            'indices': final_indices,
            'triangle_count': len(triangles)
        }

    return extracted_meshes
