#!/usr/bin/env python3
"""
tools/cooker/mesh_cooker.py

Offline 3D Model Cooker Tool for Nintendo Wii U (.wmesh binary format).
Complies with Jason Gregory's Game Engine Architecture (3rd Ed., Chapters 12 & 13):
- Native Big-Endian packing for IBM PowerPC 750CL (Espresso) and AMD Latte (R700).
- 256-byte VBO alignment for GX2 DMA direct hardware upload.
- 32-byte IBO alignment matching the Espresso L1D cache line.
- Precomputed Bounding Sphere (Ritter) and AABB for O(1) frustum culling setup.
- Tipsify-inspired Post-Transform Vertex Cache (PTVC) triangle reordering.
- Supports Wavefront OBJ input and high-quality procedural architectural models.
"""

import os
import sys
import math
import struct
import argparse
from typing import List, Tuple, Dict, Optional

# Local FBX parser import
try:
    from fbx_parser import extract_fbx_meshes
except ImportError:
    try:
        from tools.cooker.fbx_parser import extract_fbx_meshes
    except ImportError:
        extract_fbx_meshes = None

# Constants matching MeshBinaryFormat.hpp
WMESH_MAGIC = b'WMSH' # 0x574D5348
WMESH_VERSION = 1

# Flags
MESH_FLAG_STATIC       = 1 << 0
MESH_FLAG_SKINNED      = 1 << 1
MESH_FLAG_HAS_COLOR    = 1 << 2
MESH_FLAG_HAS_TEXCOORD = 1 << 3
MESH_FLAG_HAS_NORMAL   = 1 << 4
MESH_FLAG_BIG_ENDIAN   = 1 << 5
MESH_FLAG_TANGENTS     = 1 << 6

VERTEX_STRIDE = 48 # 3 pos + 3 norm + 2 uv + 4 col (12 floats * 4 = 48 bytes)

class Vertex:
    __slots__ = ('pos', 'norm', 'uv', 'col')
    def __init__(self, pos, norm=(0.0, 1.0, 0.0), uv=(0.0, 0.0), col=(1.0, 1.0, 1.0, 1.0)):
        self.pos = list(pos)
        self.norm = list(norm)
        self.uv = list(uv)
        self.col = list(col)

def align_up(offset: int, alignment: int) -> int:
    return (offset + alignment - 1) & ~(alignment - 1)

def compute_aabb(vertices: List[Vertex]) -> Tuple[Tuple[float, float, float], Tuple[float, float, float]]:
    if not vertices:
        return (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)
    min_x = min(v.pos[0] for v in vertices)
    min_y = min(v.pos[1] for v in vertices)
    min_z = min(v.pos[2] for v in vertices)
    max_x = max(v.pos[0] for v in vertices)
    max_y = max(v.pos[1] for v in vertices)
    max_z = max(v.pos[2] for v in vertices)
    return (min_x, min_y, min_z), (max_x, max_y, max_z)

def compute_bounding_sphere(vertices: List[Vertex], aabb_min, aabb_max) -> Tuple[float, float, float, float]:
    """Computes a bounding sphere using the AABB center and max distance."""
    cx = (aabb_min[0] + aabb_max[0]) * 0.5
    cy = (aabb_min[1] + aabb_max[1]) * 0.5
    cz = (aabb_min[2] + aabb_max[2]) * 0.5
    
    max_dist_sq = 0.0
    for v in vertices:
        dx = v.pos[0] - cx
        dy = v.pos[1] - cy
        dz = v.pos[2] - cz
        d_sq = dx*dx + dy*dy + dz*dz
        if d_sq > max_dist_sq:
            max_dist_sq = d_sq
            
    radius = math.sqrt(max_dist_sq)
    return (cx, cy, cz, radius)

def reorder_triangles_ptvc(indices: List[int], vertex_count: int, cache_size: int = 16) -> List[int]:
    """
    Tipsify-style triangle reordering for Post-Transform Vertex Cache (PTVC) optimization.
    Reduces GPU vertex shader invocations on the AMD Latte VGT stage.
    """
    if len(indices) < 6:
        return indices

    num_triangles = len(indices) // 3
    triangles = [indices[i*3:(i+1)*3] for i in range(num_triangles)]

    # Adjacency: vertex -> triangles
    vertex_triangles: Dict[int, List[int]] = {i: [] for i in range(vertex_count)}
    for tri_idx, tri in enumerate(triangles):
        for v in tri:
            vertex_triangles[v].append(tri_idx)

    visited_triangles = [False] * num_triangles
    reordered_indices: List[int] = []
    
    # FIFO vertex cache simulation
    cache: List[int] = []

    def get_vertex_score(v: int) -> float:
        if v not in vertex_triangles or not vertex_triangles[v]:
            return -1.0
        # If in cache, boost score
        if v in cache:
            cache_pos = cache.index(v)
            if cache_pos < 3:
                return 0.75
            return 0.5
        return 0.1

    curr_tri = 0
    while len(reordered_indices) < len(indices):
        # Pick next unvisited triangle
        if visited_triangles[curr_tri]:
            best_tri = -1
            best_score = -1.0
            # Look at triangles adjacent to cached vertices
            for cv in reversed(cache):
                for t in vertex_triangles[cv]:
                    if not visited_triangles[t]:
                        score = sum(get_vertex_score(v) for v in triangles[t])
                        if score > best_score:
                            best_score = score
                            best_tri = t
                if best_tri != -1:
                    break
            
            if best_tri == -1:
                # Find any unvisited
                for t in range(num_triangles):
                    if not visited_triangles[t]:
                        best_tri = t
                        break
            curr_tri = best_tri

        if curr_tri == -1 or visited_triangles[curr_tri]:
            break

        # Emit triangle
        tri = triangles[curr_tri]
        visited_triangles[curr_tri] = True
        reordered_indices.extend(tri)

        # Update cache
        for v in tri:
            if v in cache:
                cache.remove(v)
            cache.insert(0, v)
            if v in vertex_triangles and curr_tri in vertex_triangles[v]:
                vertex_triangles[v].remove(curr_tri)
        while len(cache) > cache_size:
            cache.pop()

    return reordered_indices if len(reordered_indices) == len(indices) else indices

def parse_obj(filepath: str, default_color=(1.0, 1.0, 1.0, 1.0)) -> Tuple[List[Vertex], List[int]]:
    """Loads a Wavefront OBJ file."""
    raw_positions = []
    raw_normals = []
    raw_uvs = []
    
    unique_verts: Dict[Tuple[int, int, int], int] = {}
    vertices: List[Vertex] = []
    indices: List[int] = []

    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = line.split()
            cmd = parts[0]
            
            if cmd == 'v':
                raw_positions.append([float(parts[1]), float(parts[2]), float(parts[3])])
            elif cmd == 'vn':
                raw_normals.append([float(parts[1]), float(parts[2]), float(parts[3])])
            elif cmd == 'vt':
                raw_uvs.append([float(parts[1]), float(parts[2])])
            elif cmd == 'f':
                face_verts = []
                for p in parts[1:]:
                    vals = p.split('/')
                    v_idx = int(vals[0]) - 1
                    vt_idx = int(vals[1]) - 1 if len(vals) > 1 and vals[1] else -1
                    vn_idx = int(vals[2]) - 1 if len(vals) > 2 and vals[2] else -1
                    
                    key = (v_idx, vt_idx, vn_idx)
                    if key not in unique_verts:
                        pos = raw_positions[v_idx]
                        norm = raw_normals[vn_idx] if vn_idx >= 0 and vn_idx < len(raw_normals) else [0.0, 1.0, 0.0]
                        uv = raw_uvs[vt_idx] if vt_idx >= 0 and vt_idx < len(raw_uvs) else [0.0, 0.0]
                        new_idx = len(vertices)
                        vertices.append(Vertex(pos, norm, uv, default_color))
                        unique_verts[key] = new_idx
                    face_verts.append(unique_verts[key])

                # Fan triangulation for n-gons
                for i in range(1, len(face_verts) - 1):
                    indices.extend([face_verts[0], face_verts[i], face_verts[i + 1]])

    # If normals were missing, compute smooth normals
    if not raw_normals:
        for v in vertices:
            v.norm = [0.0, 0.0, 0.0]
        for i in range(0, len(indices), 3):
            i0, i1, i2 = indices[i], indices[i+1], indices[i+2]
            p0, p1, p2 = vertices[i0].pos, vertices[i1].pos, vertices[i2].pos
            ux, uy, uz = p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2]
            vx, vy, vz = p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2]
            nx = uy*vz - uz*vy
            ny = uz*vx - ux*vz
            nz = ux*vy - uy*vx
            for idx in (i0, i1, i2):
                vertices[idx].norm[0] += nx
                vertices[idx].norm[1] += ny
                vertices[idx].norm[2] += nz
        for v in vertices:
            l = math.sqrt(v.norm[0]**2 + v.norm[1]**2 + v.norm[2]**2)
            if l > 1e-6:
                v.norm[0] /= l
                v.norm[1] /= l
                v.norm[2] /= l

    return vertices, indices

# ==============================================================================
# Procedural Geometry Generators for Wii U Models
# ==============================================================================

def generate_procedural_pillar(height: float = 4.0, radius: float = 0.45, flutes: int = 16) -> Tuple[List[Vertex], List[int]]:
    """
    Generates a classical stone pillar with plinth base, fluted shaft, and ionic-inspired capital.
    Ideal for ancient temple ruins in SceneEnvironmentGX2.
    """
    verts: List[Vertex] = []
    indices: List[int] = []
    
    stone_col = (0.92, 0.90, 0.85, 1.0)
    shadow_col = (0.75, 0.72, 0.68, 1.0)

    def add_quad(v0, v1, v2, v3):
        indices.extend([v0, v1, v2, v2, v1, v3])

    def add_ring(y: float, r: float, sides: int, col, v_tex: float) -> int:
        start_idx = len(verts)
        for i in range(sides + 1):
            theta = (i / sides) * 2.0 * math.pi
            ct = math.cos(theta)
            st = math.sin(theta)
            # Add subtle fluting oscillation to shaft
            flute_depth = 0.02 * math.cos(theta * flutes) if r >= radius * 0.9 else 0.0
            cur_r = r + flute_depth
            verts.append(Vertex(
                pos=[ct * cur_r, y, st * cur_r],
                norm=[ct, 0.0, st],
                uv=[(i / sides) * 3.0, v_tex],
                col=col
            ))
        return start_idx

    # 1. Base plinth (square / octagonal block)
    base_sides = 16
    r_base = radius * 1.55
    r_torus = radius * 1.35
    
    # Bottom plinth ring
    b0 = add_ring(0.0, r_base, base_sides, shadow_col, 0.0)
    b1 = add_ring(0.20, r_base, base_sides, stone_col, 0.1)
    for i in range(base_sides):
        add_quad(b0 + i, b0 + i + 1, b1 + i, b1 + i + 1)

    # Torus base transition
    b2 = add_ring(0.35, r_torus, base_sides, stone_col, 0.2)
    for i in range(base_sides):
        add_quad(b1 + i, b1 + i + 1, b2 + i, b2 + i + 1)

    # 2. Fluted Shaft
    shaft_sides = 24
    num_shaft_slices = 8
    shaft_bottom_y = 0.40
    shaft_top_y = height - 0.45
    
    last_ring = add_ring(shaft_bottom_y, radius, shaft_sides, stone_col, 0.25)
    for s in range(1, num_shaft_slices + 1):
        frac = s / num_shaft_slices
        y = shaft_bottom_y + (shaft_top_y - shaft_bottom_y) * frac
        # Slight classical entasis (shaft is slightly wider in middle)
        entasis = math.sin(frac * math.pi) * 0.03
        r_shaft = (radius * (1.0 - 0.08 * frac)) + entasis
        cur_ring = add_ring(y, r_shaft, shaft_sides, stone_col, 0.25 + frac * 2.0)
        for i in range(shaft_sides):
            add_quad(last_ring + i, last_ring + i + 1, cur_ring + i, cur_ring + i + 1)
        last_ring = cur_ring

    # 3. Capital (Echinus + Abacus)
    cap_sides = 16
    c0 = add_ring(height - 0.40, radius * 1.25, cap_sides, stone_col, 2.3)
    c1 = add_ring(height - 0.15, radius * 1.50, cap_sides, stone_col, 2.45)
    c2 = add_ring(height, radius * 1.60, cap_sides, stone_col, 2.6)
    
    for i in range(cap_sides):
        add_quad(c0 + i, c0 + i + 1, c1 + i, c1 + i + 1)
        add_quad(c1 + i, c1 + i + 1, c2 + i, c2 + i + 1)

    # Top cap disc
    top_center_idx = len(verts)
    verts.append(Vertex([0.0, height, 0.0], [0.0, 1.0, 0.0], [0.5, 0.5], stone_col))
    for i in range(cap_sides):
        indices.extend([top_center_idx, c2 + i, c2 + i + 1])

    # Bottom cap disc
    bottom_center_idx = len(verts)
    verts.append(Vertex([0.0, 0.0, 0.0], [0.0, -1.0, 0.0], [0.5, 0.5], shadow_col))
    for i in range(base_sides):
        indices.extend([bottom_center_idx, b0 + i + 1, b0 + i])

    return verts, indices

def generate_procedural_obelisk(height: float = 5.2, base_w: float = 0.9, tip_ratio: float = 0.55) -> Tuple[List[Vertex], List[int]]:
    """
    Generates an ancient carved stone monolith / obelisk with pyramidion apex.
    """
    verts: List[Vertex] = []
    indices: List[int] = []
    
    col_dark = (0.78, 0.76, 0.72, 1.0)
    col_light = (0.95, 0.92, 0.88, 1.0)
    col_gold = (1.0, 0.85, 0.25, 1.0) # Gold gilded pyramidion cap

    def add_quad_face(p0, p1, p2, p3, norm, u0, u1, col):
        b = len(verts)
        verts.append(Vertex(p0, norm, [u0, 0.0], col))
        verts.append(Vertex(p1, norm, [u1, 0.0], col))
        verts.append(Vertex(p2, norm, [u0, 1.0], col))
        verts.append(Vertex(p3, norm, [u1, 1.0], col))
        indices.extend([b, b+1, b+2, b+2, b+1, b+3])

    # 1. Base pedestal
    b_half = base_w * 0.75
    b_h = 0.35
    # Front
    add_quad_face([-b_half, 0.0, b_half], [b_half, 0.0, b_half], [-b_half, b_h, b_half], [b_half, b_h, b_half], [0, 0, 1], 0, 1, col_dark)
    # Back
    add_quad_face([b_half, 0.0, -b_half], [-b_half, 0.0, -b_half], [b_half, b_h, -b_half], [-b_half, b_h, -b_half], [0, 0, -1], 0, 1, col_dark)
    # Left
    add_quad_face([-b_half, 0.0, -b_half], [-b_half, 0.0, b_half], [-b_half, b_h, -b_half], [-b_half, b_h, b_half], [-1, 0, 0], 0, 1, col_dark)
    # Right
    add_quad_face([b_half, 0.0, b_half], [b_half, 0.0, -b_half], [b_half, b_h, b_half], [b_half, b_h, -b_half], [1, 0, 0], 0, 1, col_dark)

    # 2. Main tapered shaft
    w0 = base_w * 0.5
    w1 = w0 * tip_ratio
    h_shaft_bottom = b_h
    h_shaft_top = height - 0.75

    # 4 trapezoidal sides
    # Front (+Z)
    add_quad_face([-w0, h_shaft_bottom, w0], [w0, h_shaft_bottom, w0], [-w1, h_shaft_top, w1], [w1, h_shaft_top, w1], [0.0, 0.08, 0.99], 0, 1, col_light)
    # Back (-Z)
    add_quad_face([w0, h_shaft_bottom, -w0], [-w0, h_shaft_bottom, -w0], [w1, h_shaft_top, -w1], [-w1, h_shaft_top, -w1], [0.0, 0.08, -0.99], 0, 1, col_light)
    # Left (-X)
    add_quad_face([-w0, h_shaft_bottom, -w0], [-w0, h_shaft_bottom, w0], [-w1, h_shaft_top, -w1], [-w1, h_shaft_top, w1], [-0.99, 0.08, 0.0], 0, 1, col_light)
    # Right (+X)
    add_quad_face([w0, h_shaft_bottom, w0], [w0, h_shaft_bottom, -w0], [w1, h_shaft_top, w1], [w1, h_shaft_top, -w1], [0.99, 0.08, 0.0], 0, 1, col_light)

    # 3. Pyramidion apex
    apex_y = height
    def add_tri_face(p0, p1, p2, norm, col):
        b = len(verts)
        verts.append(Vertex(p0, norm, [0.0, 0.0], col))
        verts.append(Vertex(p1, norm, [1.0, 0.0], col))
        verts.append(Vertex(p2, norm, [0.5, 1.0], col))
        indices.extend([b, b+1, b+2])

    p_apex = [0.0, apex_y, 0.0]
    add_tri_face([-w1, h_shaft_top, w1], [w1, h_shaft_top, w1], p_apex, [0.0, 0.5, 0.86], col_gold)
    add_tri_face([w1, h_shaft_top, -w1], [-w1, h_shaft_top, -w1], p_apex, [0.0, 0.5, -0.86], col_gold)
    add_tri_face([-w1, h_shaft_top, -w1], [-w1, h_shaft_top, w1], p_apex, [-0.86, 0.5, 0.0], col_gold)
    add_tri_face([w1, h_shaft_top, w1], [w1, h_shaft_top, -w1], p_apex, [0.86, 0.5, 0.0], col_gold)

    return verts, indices

def generate_procedural_beacon(height: float = 4.5, radius: float = 0.8) -> Tuple[List[Vertex], List[int]]:
    """
    Generates a sci-fi energy beacon pylon with tripod legs and floating core.
    """
    verts: List[Vertex] = []
    indices: List[int] = []
    
    metal_dark = (0.25, 0.28, 0.35, 1.0)
    glow_cyan = (0.20, 0.95, 0.90, 1.0)
    gold_trim = (0.95, 0.80, 0.20, 1.0)

    # 3 tripod legs
    num_legs = 3
    for leg in range(num_legs):
        angle = (leg / num_legs) * 2.0 * math.pi
        ca = math.cos(angle)
        sa = math.sin(angle)
        
        # Base strut
        p0 = [ca * radius * 1.5, 0.0, sa * radius * 1.5]
        p1 = [ca * radius * 0.4, height * 0.65, sa * radius * 0.4]
        p2 = [ca * radius * 0.5, height * 0.70, sa * radius * 0.5]
        
        b = len(verts)
        verts.append(Vertex(p0, [ca, 0.2, sa], [0.0, 0.0], metal_dark))
        verts.append(Vertex(p1, [ca, 0.5, sa], [1.0, 0.0], gold_trim))
        verts.append(Vertex(p2, [ca, 0.5, sa], [0.5, 1.0], metal_dark))
        indices.extend([b, b+1, b+2])

    # Central glowing crystal orb (octahedron)
    core_y = height * 0.60
    r_core = 0.35
    top_p = [0.0, core_y + r_core, 0.0]
    bot_p = [0.0, core_y - r_core, 0.0]
    ring_pts = [
        [r_core, core_y, 0.0],
        [0.0, core_y, r_core],
        [-r_core, core_y, 0.0],
        [0.0, core_y, -r_core]
    ]

    for i in range(4):
        p_cur = ring_pts[i]
        p_next = ring_pts[(i + 1) % 4]
        
        # Upper cone
        b = len(verts)
        verts.append(Vertex(top_p, [0, 1, 0], [0.5, 1.0], glow_cyan))
        verts.append(Vertex(p_cur, [p_cur[0], 0, p_cur[2]], [0.0, 0.0], glow_cyan))
        verts.append(Vertex(p_next, [p_next[0], 0, p_next[2]], [1.0, 0.0], glow_cyan))
        indices.extend([b, b+1, b+2])
        
        # Lower cone
        b = len(verts)
        verts.append(Vertex(bot_p, [0, -1, 0], [0.5, 0.0], glow_cyan))
        verts.append(Vertex(p_next, [p_next[0], 0, p_next[2]], [1.0, 1.0], glow_cyan))
        verts.append(Vertex(p_cur, [p_cur[0], 0, p_cur[2]], [0.0, 1.0], glow_cyan))
        indices.extend([b, b+1, b+2])

    return verts, indices

def generate_procedural_ground(size: float = 100.0, subdivisions: int = 16, tiling: float = 40.0) -> Tuple[List[Vertex], List[int]]:
    """
    Generates a horizontal subdivided ground plane centered at Y = 0.0 with upward normals (0, 1, 0)
    and tiled UV texture coordinates.
    """
    verts: List[Vertex] = []
    indices: List[int] = []
    
    half_size = size * 0.5
    step = size / subdivisions
    norm = [0.0, 1.0, 0.0]
    col = (1.0, 1.0, 1.0, 1.0)
    
    # Generate grid vertices
    for j in range(subdivisions + 1):
        z = -half_size + j * step
        v = (j / subdivisions) * tiling
        for i in range(subdivisions + 1):
            x = -half_size + i * step
            u = (i / subdivisions) * tiling
            verts.append(Vertex([x, 0.0, z], norm, [u, v], col))
            
    # Generate quad triangle indices (CCW winding viewed from above)
    stride = subdivisions + 1
    for j in range(subdivisions):
        for i in range(subdivisions):
            p0 = j * stride + i
            p1 = j * stride + (i + 1)
            p2 = (j + 1) * stride + i
            p3 = (j + 1) * stride + (i + 1)
            
            # Triangle 1: p0, p2, p1
            indices.extend([p0, p2, p1])
            # Triangle 2: p1, p2, p3
            indices.extend([p1, p2, p3])
            
    return verts, indices

# ==============================================================================
# Cooker Serialization
# ==============================================================================

def cook_model(vertices: List[Vertex], indices: List[int], output_filepath: str, optimize_cache: bool = True):
    """
    Serializes vertices and indices into the binary .wmesh format.
    Enforces Big-Endian PowerPC encoding, 256B VBO alignment and 32B IBO alignment.
    """
    vertex_count = len(vertices)
    index_count = len(indices)

    if vertex_count > 65535:
        index_format = 1 # U32
        index_stride = 4
        index_pack_fmt = '>I'
    else:
        index_format = 0 # U16
        index_stride = 2
        index_pack_fmt = '>H'

    # 1. PTVC Triangle Reordering
    if optimize_cache:
        indices = reorder_triangles_ptvc(indices, vertex_count, cache_size=16)

    # 2. Bounding Volumes
    aabb_min, aabb_max = compute_aabb(vertices)
    cx, cy, cz, radius = compute_bounding_sphere(vertices, aabb_min, aabb_max)

    # 3. Calculate Layout Offsets with Hardware Alignment
    # Header is 96 bytes
    header_size = 96
    vbo_offset = align_up(header_size, 256) # 256 bytes for GX2 DMA
    vbo_size = vertex_count * VERTEX_STRIDE
    
    raw_ibo_offset = vbo_offset + vbo_size
    ibo_offset = align_up(raw_ibo_offset, 32) # 32 bytes for Espresso L1D
    ibo_size = index_count * index_stride
    
    total_file_size = ibo_offset + ibo_size

    flags = (MESH_FLAG_STATIC | 
             MESH_FLAG_HAS_COLOR | 
             MESH_FLAG_HAS_TEXCOORD | 
             MESH_FLAG_HAS_NORMAL | 
             MESH_FLAG_BIG_ENDIAN)

    # 4. Pack Header in Big-Endian ('>')
    # uint32 magic
    # uint16 version, flags
    # uint32 vertexCount, indexCount, vertexStride, indexFormat, vboOffset, vboSize, iboOffset, iboSize
    # float boundingSphere[4]
    # float aabbMin[3]
    # float aabbMax[3]
    # uint32 submeshCount
    # uint32 reserved[3]
    header_bytes = struct.pack(
        '>4sHHIIIIIIII4f3f3fI3I',
        WMESH_MAGIC,
        WMESH_VERSION,
        flags,
        vertex_count,
        index_count,
        VERTEX_STRIDE,
        index_format,
        vbo_offset,
        vbo_size,
        ibo_offset,
        ibo_size,
        cx, cy, cz, radius,
        aabb_min[0], aabb_min[1], aabb_min[2],
        aabb_max[0], aabb_max[1], aabb_max[2],
        1, # submeshCount = 1
        0, 0, 0 # reserved
    )
    assert len(header_bytes) == 96, f"Header size is {len(header_bytes)}, expected 96"

    # Write file
    os.makedirs(os.path.dirname(os.path.abspath(output_filepath)), exist_ok=True)
    with open(output_filepath, 'wb') as f:
        # Write header
        f.write(header_bytes)
        
        # Pad to VBO offset
        pad_vbo = vbo_offset - f.tell()
        if pad_vbo > 0:
            f.write(b'\x00' * pad_vbo)
            
        assert f.tell() == vbo_offset, f"VBO offset mismatch: {f.tell()} vs {vbo_offset}"

        # Write VBO data: each vertex is 12 floats (48 bytes) in Big-Endian
        for v in vertices:
            v_bytes = struct.pack(
                '>3f3f2f4f',
                v.pos[0], v.pos[1], v.pos[2],
                v.norm[0], v.norm[1], v.norm[2],
                v.uv[0], v.uv[1],
                v.col[0], v.col[1], v.col[2], v.col[3]
            )
            f.write(v_bytes)

        # Pad to IBO offset
        pad_ibo = ibo_offset - f.tell()
        if pad_ibo > 0:
            f.write(b'\x00' * pad_ibo)
            
        assert f.tell() == ibo_offset, f"IBO offset mismatch: {f.tell()} vs {ibo_offset}"

        # Write IBO data: indices in Big-Endian
        for idx in indices:
            f.write(struct.pack(index_pack_fmt, idx))

    print(f"[WMESH COOKER] Exito: '{output_filepath}'")
    print(f"  - Vertices: {vertex_count} (Stride {VERTEX_STRIDE}B, VBO {vbo_size}B @ offset 0x{vbo_offset:X})")
    print(f"  - Indices:  {index_count} ({'U16' if index_format == 0 else 'U32'}, IBO {ibo_size}B @ offset 0x{ibo_offset:X})")
    print(f"  - AABB:     [{aabb_min[0]:.2f}, {aabb_min[1]:.2f}, {aabb_min[2]:.2f}] -> [{aabb_max[0]:.2f}, {aabb_max[1]:.2f}, {aabb_max[2]:.2f}]")
    print(f"  - Esfera:   Centro ({cx:.2f}, {cy:.2f}, {cz:.2f}), Radio {radius:.2f}m")
    print(f"  - Big-Endian: SI (PPC / Latte directo)")
    print(f"  - Tamano Total: {total_file_size} bytes")

def encode_dxt1_image(rgba_bytes: bytes, width: int, height: int) -> bytes:
    """
    Fast Bounding-Box Endpoint BC1 (DXT1) encoder in pure Python.
    Produces 8 bytes per 4x4 block (4 bpp, 8:1 compression ratio).
    Packs two RGB565 endpoints and 16 2-bit interpolation indices per 4x4 block.
    """
    bw = max(1, (width + 3) // 4)
    bh = max(1, (height + 3) // 4)
    out = bytearray(bw * bh * 8)
    mv = memoryview(rgba_bytes)
    out_idx = 0
    row_stride = width * 4

    for by in range(bh):
        y0 = by * 4
        for bx in range(bw):
            x0 = bx * 4
            min_r = min_g = min_b = 255
            max_r = max_g = max_b = 0
            pixels = []

            for dy in range(4):
                py = min(height - 1, y0 + dy)
                row_off = py * row_stride
                for dx in range(4):
                    px = min(width - 1, x0 + dx)
                    off = row_off + px * 4
                    r = mv[off]
                    g = mv[off + 1]
                    b = mv[off + 2]
                    pixels.append((r, g, b))
                    if r < min_r: min_r = r
                    if g < min_g: min_g = g
                    if b < min_b: min_b = b
                    if r > max_r: max_r = r
                    if g > max_g: max_g = g
                    if b > max_b: max_b = b

            # Inset bounding box by 1/16 to reduce RMS quantization error (Simon Brown / van Waveren)
            ir = (max_r - min_r) >> 4
            ig = (max_g - min_g) >> 4
            ib = (max_b - min_b) >> 4
            min_r = min(255, min_r + ir)
            min_g = min(255, min_g + ig)
            min_b = min(255, min_b + ib)
            max_r = max(0, max_r - ir)
            max_g = max(0, max_g - ig)
            max_b = max(0, max_b - ib)

            c0 = ((max_r >> 3) << 11) | ((max_g >> 2) << 5) | (max_b >> 3)
            c1 = ((min_r >> 3) << 11) | ((min_g >> 2) << 5) | (min_b >> 3)

            if c0 < c1:
                c0, c1 = c1, c0
                min_r, max_r = max_r, min_r
                min_g, max_g = max_g, min_g
                min_b, max_b = max_b, min_b
            elif c0 == c1:
                out[out_idx:out_idx + 8] = struct.pack('<HHI', c0, c1, 0)
                out_idx += 8
                continue

            # 4-color palette along principal axis
            p0r, p0g, p0b = max_r, max_g, max_b
            p1r, p1g, p1b = min_r, min_g, min_b
            p2r = (2 * p0r + p1r) // 3
            p2g = (2 * p0g + p1g) // 3
            p2b = (2 * p0b + p1b) // 3
            p3r = (p0r + 2 * p1r) // 3
            p3g = (p0g + 2 * p1g) // 3
            p3b = (p0b + 2 * p1b) // 3

            indices = 0
            for i, (r, g, b) in enumerate(pixels):
                d0 = (r - p0r) ** 2 + (g - p0g) ** 2 + (b - p0b) ** 2
                d1 = (r - p1r) ** 2 + (g - p1g) ** 2 + (b - p1b) ** 2
                d2 = (r - p2r) ** 2 + (g - p2g) ** 2 + (b - p2b) ** 2
                d3 = (r - p3r) ** 2 + (g - p3g) ** 2 + (b - p3b) ** 2
                best = 0
                best_d = d0
                if d1 < best_d:
                    best_d = d1
                    best = 1
                if d2 < best_d:
                    best_d = d2
                    best = 2
                if d3 < best_d:
                    best = 3
                indices |= (best << (2 * i))

            out[out_idx:out_idx + 8] = struct.pack('<HHI', c0, c1, indices)
            out_idx += 8

    return bytes(out)


def encode_dxt5_image(rgba_bytes: bytes, width: int, height: int) -> bytes:
    """
    Fast Bounding-Box Endpoint BC3 (DXT5) encoder in pure Python.
    Produces 16 bytes per 4x4 block (8 bpp, 4:1 compression ratio):
    - 8-byte interpolated alpha block
    - 8-byte BC1 color block
    """
    bw = max(1, (width + 3) // 4)
    bh = max(1, (height + 3) // 4)
    color_blocks = encode_dxt1_image(rgba_bytes, width, height)
    out = bytearray(bw * bh * 16)
    mv = memoryview(rgba_bytes)
    row_stride = width * 4
    block_idx = 0

    for by in range(bh):
        y0 = by * 4
        for bx in range(bw):
            x0 = bx * 4
            min_a = 255
            max_a = 0
            alphas = []
            for dy in range(4):
                py = min(height - 1, y0 + dy)
                row_off = py * row_stride
                for dx in range(4):
                    px = min(width - 1, x0 + dx)
                    a = mv[row_off + px * 4 + 3]
                    alphas.append(a)
                    if a < min_a: min_a = a
                    if a > max_a: max_a = a

            a0 = max_a
            a1 = min_a
            alpha_bits = 0
            if a0 != a1:
                span = a0 - a1
                for i, a in enumerate(alphas):
                    # Map smoothly to 8-level interpolation table (0=a0, 1=a1, 2..7=interpolated)
                    step = ((a0 - a) * 7 + (span // 2)) // span
                    if step <= 0:
                        code = 0
                    elif step >= 7:
                        code = 1
                    else:
                        code = step + 1
                    alpha_bits |= (code << (3 * i))

            out_off = block_idx * 16
            out[out_off] = a0
            out[out_off + 1] = a1
            for b_i in range(6):
                out[out_off + 2 + b_i] = (alpha_bits >> (8 * b_i)) & 0xFF
            c_off = block_idx * 8
            out[out_off + 8:out_off + 16] = color_blocks[c_off:c_off + 8]
            block_idx += 1

    return bytes(out)


def cook_texture(input_png: str, output_wtex: str, target_size: int = 2048, tex_format: str = 'rgba8'):
    """
    Cooks an input PNG texture into a Big-Endian .wtex file with a full Lanczos mipmap pyramid.
    Supports:
    - format 1 ('rgba8'): Uncompressed RGBA8 (32 bpp)
    - format 2 ('bc1'):   Hardware Block Compression BC1 / DXT1 (4 bpp, 8:1 compression)
    - format 3 ('bc3'):   Hardware Block Compression BC3 / DXT5 (8 bpp, 4:1 compression)
    Header (16 bytes):
    - magic:     'WTEX' (4B)
    - width:     uint32 BE
    - height:    uint32 BE
    - mipFormat: uint32 BE -> bits[0..15] = mipLevels, bits[16..31] = formatCode (0/1=RGBA8, 2=BC1, 3=BC3)
    """
    from PIL import Image
    img = Image.open(input_png).convert('RGBA')
    if img.size != (target_size, target_size):
        img = img.resize((target_size, target_size), Image.Resampling.LANCZOS)

    fmt_lower = tex_format.lower()
    if fmt_lower in ('bc1', 'dxt1'):
        fmt_code = 2
    elif fmt_lower in ('bc3', 'dxt5'):
        fmt_code = 3
    else:
        fmt_code = 1

    mip_levels = int(math.log2(target_size)) + 1
    mip_payloads = []
    cur_img = img
    cur_dim = target_size
    for level in range(mip_levels):
        if level > 0:
            cur_img = img.resize((cur_dim, cur_dim), Image.Resampling.LANCZOS)
        raw_rgba = cur_img.tobytes()
        if fmt_code == 2:
            mip_payloads.append(encode_dxt1_image(raw_rgba, cur_dim, cur_dim))
        elif fmt_code == 3:
            mip_payloads.append(encode_dxt5_image(raw_rgba, cur_dim, cur_dim))
        else:
            mip_payloads.append(raw_rgba)
        cur_dim = max(1, cur_dim // 2)

    # Encode format in upper 16 bits of word 3 when fmt_code > 1 (100% backward compatible with v1 readers)
    packed_mip_fmt = mip_levels if fmt_code == 1 else ((fmt_code << 16) | (mip_levels & 0xFFFF))
    header = struct.pack('>4sIII', b'WTEX', target_size, target_size, packed_mip_fmt)
    os.makedirs(os.path.dirname(os.path.abspath(output_wtex)), exist_ok=True)
    total_bytes = len(header)
    with open(output_wtex, 'wb') as f:
        f.write(header)
        for payload in mip_payloads:
            f.write(payload)
            total_bytes += len(payload)
    print(f"[WTEX COOKER] Exito: '{output_wtex}' ({target_size}x{target_size} {fmt_lower.upper()}, {mip_levels} mipmaps, {total_bytes} bytes)")

def main():
    parser = argparse.ArgumentParser(description="Wii U 3D Model & Texture Cooker (.wmesh / .wtex)")
    parser.add_argument('--input', '-i', type=str, help="Ruta al archivo Wavefront OBJ o FBX")
    parser.add_argument('--cook-texture', '-t', type=str, help="Ruta al archivo PNG para cocinar a formato .wtex")
    parser.add_argument('--tex-size', type=int, default=2048, help="Resolucion cuadrada de textura .wtex (def: 2048)")
    parser.add_argument('--tex-format', type=str, default='rgba8', choices=['rgba8', 'bc1', 'bc3'], help="Formato de textura .wtex: rgba8 (def), bc1 (DXT1 8:1), bc3 (DXT5 4:1)")
    parser.add_argument('--mesh', '-m', type=str, help="Submalla/nodo específico a cocinar dentro del archivo FBX (o 'all' para combinar)")
    parser.add_argument('--procedural', '-p', type=str, choices=['pillar', 'obelisk', 'beacon', 'ground', 'plane'], help="Generar modelo procedural")
    parser.add_argument('--output', '-o', type=str, required=True, help="Ruta de destino .wmesh o .wtex")
    parser.add_argument('--scale', '-s', type=float, default=1.0, help="Factor de escala geometrica")
    parser.add_argument('--no-cache-opt', action='store_true', help="Desactivar optimizacion PTVC")
    
    args = parser.parse_args()

    if args.cook_texture:
        if not os.path.exists(args.cook_texture):
            print(f"Error: Textura de entrada no encontrada: {args.cook_texture}")
            sys.exit(1)
        cook_texture(args.cook_texture, args.output, args.tex_size, args.tex_format)
    elif args.procedural:
        print(f"[WMESH COOKER] Generando modelo procedural: '{args.procedural}'...")
        if args.procedural == 'pillar':
            verts, indices = generate_procedural_pillar()
        elif args.procedural == 'obelisk':
            verts, indices = generate_procedural_obelisk()
        elif args.procedural == 'beacon':
            verts, indices = generate_procedural_beacon()
        elif args.procedural in ('ground', 'plane'):
            verts, indices = generate_procedural_ground(size=100.0, subdivisions=16, tiling=40.0)
        else:
            print(f"Tipo procedural desconocido: {args.procedural}")
            sys.exit(1)
        if args.scale != 1.0:
            for v in verts:
                v.pos[0] *= args.scale
                v.pos[1] *= args.scale
                v.pos[2] *= args.scale
        cook_model(verts, indices, args.output, optimize_cache=(not args.no_cache_opt))
    elif args.input:
        if not os.path.exists(args.input):
            print(f"Error: Archivo de entrada no encontrado: {args.input}")
            sys.exit(1)
            
        ext = os.path.splitext(args.input)[1].lower()
        if ext == '.fbx':
            if extract_fbx_meshes is None:
                print("Error: No se pudo cargar el módulo fbx_parser.")
                sys.exit(1)
            print(f"[WMESH COOKER] Extrayendo mallas desde FBX: '{args.input}'...")
            fbx_meshes = extract_fbx_meshes(args.input)
            if not fbx_meshes:
                print("Error: No se encontraron geometrías válidas en el archivo FBX.")
                sys.exit(1)
            print(f"  - Submallas detectadas: {list(fbx_meshes.keys())}")
            
            # Si se solicita una submalla específica o 'all'
            if args.mesh and args.mesh.lower() not in ('all', 'combined'):
                target_key = None
                for k in fbx_meshes:
                    if args.mesh.lower() in k.lower():
                        target_key = k
                        break
                if not target_key:
                    print(f"Error: Submalla '{args.mesh}' no encontrada en FBX. Opciones disponibles: {list(fbx_meshes.keys())}")
                    sys.exit(1)
                print(f"  - Cocinando submalla seleccionada: '{target_key}'...")
                m = fbx_meshes[target_key]
                verts = [Vertex(v['pos'], v['norm'], v['uv'], v['col']) for v in m['vertices']]
                indices = m['indices']
                if args.scale != 1.0:
                    for v in verts:
                        v.pos[0] *= args.scale
                        v.pos[1] *= args.scale
                        v.pos[2] *= args.scale
                cook_model(verts, indices, args.output, optimize_cache=(not args.no_cache_opt))
            elif args.mesh and args.mesh.lower() in ('all', 'combined'):
                print("  - Combinando todas las submallas en un único modelo .wmesh...")
                all_verts = []
                all_indices = []
                for k, m in fbx_meshes.items():
                    base_idx = len(all_verts)
                    v_list = [Vertex(v['pos'], v['norm'], v['uv'], v['col']) for v in m['vertices']]
                    all_verts.extend(v_list)
                    all_indices.extend([i + base_idx for i in m['indices']])
                if args.scale != 1.0:
                    for v in all_verts:
                        v.pos[0] *= args.scale
                        v.pos[1] *= args.scale
                        v.pos[2] *= args.scale
                cook_model(all_verts, all_indices, args.output, optimize_cache=(not args.no_cache_opt))
            else:
                out_dir = os.path.dirname(os.path.abspath(args.output))
                base_name = os.path.splitext(os.path.basename(args.output))[0]
                
                primary_key = 'house' if 'house' in fbx_meshes else list(fbx_meshes.keys())[0]
                print(f"  - Malla principal seleccionada para '{args.output}': '{primary_key}'")
                
                for k, m in fbx_meshes.items():
                    if k == primary_key:
                        target_path = args.output
                    else:
                        target_path = os.path.join(out_dir, f"{base_name}_{k}.wmesh")
                    verts = [Vertex(v['pos'], v['norm'], v['uv'], v['col']) for v in m['vertices']]
                    indices = m['indices']
                    if args.scale != 1.0:
                        for v in verts:
                            v.pos[0] *= args.scale
                            v.pos[1] *= args.scale
                            v.pos[2] *= args.scale
                    cook_model(verts, indices, target_path, optimize_cache=(not args.no_cache_opt))
                    
                complete_path = os.path.join(out_dir, f"{base_name}_complete.wmesh")
                all_verts = []
                all_indices = []
                for k, m in fbx_meshes.items():
                    base_idx = len(all_verts)
                    v_list = [Vertex(v['pos'], v['norm'], v['uv'], v['col']) for v in m['vertices']]
                    all_verts.extend(v_list)
                    all_indices.extend([i + base_idx for i in m['indices']])
                if args.scale != 1.0:
                    for v in all_verts:
                        v.pos[0] *= args.scale
                        v.pos[1] *= args.scale
                        v.pos[2] *= args.scale
                cook_model(all_verts, all_indices, complete_path, optimize_cache=(not args.no_cache_opt))
        else:
            print(f"[WMESH COOKER] Cargando archivo OBJ: '{args.input}'...")
            verts, indices = parse_obj(args.input)
            if args.scale != 1.0:
                for v in verts:
                    v.pos[0] *= args.scale
                    v.pos[1] *= args.scale
                    v.pos[2] *= args.scale
            cook_model(verts, indices, args.output, optimize_cache=(not args.no_cache_opt))
    else:
        print("Error: Debe especificar --input <archivo.obj|archivo.fbx> o --procedural <pillar|obelisk|beacon>")
        sys.exit(1)

if __name__ == '__main__':
    main()
