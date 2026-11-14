#!/usr/bin/env python3
"""
tools/cooker/generate_skinned_character.py

Generador de Personaje Articulado Procedural con Animación Esquelética
para Nintendo Wii U (Latte GX2 Linear Blend Skinning).
Genera:
1. assets/models/character.wmesh (Malla esquelética Big-Endian con stride de 80 bytes)
2. assets/models/character.wskel (Jerarquía de huesos y matrices de bind pose)
3. assets/models/character.wanim (Clips de animación: Idle, Walk, Wave_Flex)
"""

import os
import sys
import math
import struct
from typing import List, Tuple, Dict

WMESH_MAGIC = b'WMSH'
WSKL_MAGIC  = b'WSKL'
WANM_MAGIC  = b'WANM'

MESH_FLAG_STATIC       = 1 << 0
MESH_FLAG_SKINNED      = 1 << 1
MESH_FLAG_HAS_COLOR    = 1 << 2
MESH_FLAG_HAS_TEXCOORD = 1 << 3
MESH_FLAG_HAS_NORMAL   = 1 << 4
MESH_FLAG_BIG_ENDIAN   = 1 << 5

SKINNED_VERTEX_STRIDE = 80

# -----------------------------------------------------------------------------
# Matriz 4x4 y Cuaternión en Python (Column-Major para coincidir con OpenGL/GX2)
# -----------------------------------------------------------------------------
def mat4_identity():
    return [
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    ]

def mat4_translation(tx, ty, tz):
    m = mat4_identity()
    m[12] = tx
    m[13] = ty
    m[14] = tz
    return m

def quat_identity():
    return (0.0, 0.0, 0.0, 1.0)

def quat_from_axis_angle(axis, angle_rad):
    ha = angle_rad * 0.5
    s = math.sin(ha)
    length = math.sqrt(axis[0]**2 + axis[1]**2 + axis[2]**2)
    if length > 1e-6:
        nx, ny, nz = axis[0]/length, axis[1]/length, axis[2]/length
    else:
        nx, ny, nz = 0.0, 1.0, 0.0
    return (nx * s, ny * s, nz * s, math.cos(ha))

def quat_to_mat4(q):
    x, y, z, w = q
    xx, yy, zz = x*x, y*y, z*z
    xy, xz, yz = x*y, x*z, y*z
    wx, wy, wz = w*x, w*y, w*z
    return [
        1.0 - 2.0*(yy + zz), 2.0*(xy + wz),       2.0*(xz - wy),       0.0,
        2.0*(xy - wz),       1.0 - 2.0*(xx + zz), 2.0*(yz + wx),       0.0,
        2.0*(xz + wy),       2.0*(yz - wx),       1.0 - 2.0*(xx + yy), 0.0,
        0.0,                 0.0,                 0.0,                 1.0
    ]

def mat4_from_trs(t, q, s=(1.0, 1.0, 1.0)):
    rot = quat_to_mat4(q)
    return [
        rot[0]*s[0], rot[1]*s[0], rot[2]*s[0], 0.0,
        rot[4]*s[1], rot[5]*s[1], rot[6]*s[1], 0.0,
        rot[8]*s[2], rot[9]*s[2], rot[10]*s[2], 0.0,
        t[0],        t[1],        t[2],         1.0
    ]

def mat4_mul(a, b):
    # Column-major a * b
    out = [0.0] * 16
    for c in range(4):
        for r in range(4):
            out[c*4 + r] = (
                a[0*4 + r] * b[c*4 + 0] +
                a[1*4 + r] * b[c*4 + 1] +
                a[2*4 + r] * b[c*4 + 2] +
                a[3*4 + r] * b[c*4 + 3]
            )
    return out

def mat4_invert_affine(m):
    # Traspuesta de la submatriz 3x3 y traslación inversa
    inv = mat4_identity()
    inv[0] = m[0]; inv[1] = m[4]; inv[2] = m[8]
    inv[4] = m[1]; inv[5] = m[5]; inv[6] = m[9]
    inv[8] = m[2]; inv[9] = m[6]; inv[10] = m[10]

    inv[12] = -(inv[0]*m[12] + inv[4]*m[13] + inv[8]*m[14])
    inv[13] = -(inv[1]*m[12] + inv[5]*m[13] + inv[9]*m[14])
    inv[14] = -(inv[2]*m[12] + inv[6]*m[13] + inv[10]*m[14])
    return inv

# -----------------------------------------------------------------------------
# Definición del Esqueleto (16 Articulaciones)
# -----------------------------------------------------------------------------
JOINTS_DEF = [
    # (name, parent_idx, local_pos)
    ("Pelvis",     -1, (0.0, 1.80, 0.0)),
    ("Spine",       0, (0.0, 0.45, 0.0)),
    ("Chest",       1, (0.0, 0.50, 0.0)),
    ("Neck_Head",   2, (0.0, 0.55, 0.0)),
    ("L_Shoulder",  2, (0.55, 0.05, 0.0)),
    ("L_Elbow",     4, (0.50, 0.0, 0.0)),
    ("L_Hand",      5, (0.50, 0.0, 0.0)),
    ("R_Shoulder",  2, (-0.55, 0.05, 0.0)),
    ("R_Elbow",     7, (-0.50, 0.0, 0.0)),
    ("R_Hand",      8, (-0.50, 0.0, 0.0)),
    ("L_Hip",       0, (0.35, -0.15, 0.0)),
    ("L_Knee",     10, (0.0, -0.75, 0.0)),
    ("L_Foot",     11, (0.0, -0.75, 0.1)),
    ("R_Hip",       0, (-0.35, -0.15, 0.0)),
    ("R_Knee",     13, (0.0, -0.75, 0.0)),
    ("R_Foot",     14, (0.0, -0.75, 0.1)),
]

def build_skeleton():
    joints = []
    # 1. Poses locales en bind pose
    for name, parent, lpos in JOINTS_DEF:
        lmat = mat4_translation(lpos[0], lpos[1], lpos[2])
        joints.append({
            'name': name,
            'parent': parent,
            'local_bind': lmat,
            'world_bind': None,
            'inv_bind': None
        })

    # 2. Poses mundiales forward O(N)
    for i, j in enumerate(joints):
        if j['parent'] < 0:
            j['world_bind'] = list(j['local_bind'])
        else:
            j['world_bind'] = mat4_mul(joints[j['parent']]['world_bind'], j['local_bind'])
        j['inv_bind'] = mat4_invert_affine(j['world_bind'])

    return joints

# -----------------------------------------------------------------------------
# Generador de Geometría con Pesos de Skinning
# -----------------------------------------------------------------------------
class SkinnedVertex:
    def __init__(self, pos, norm, uv, col, bones, weights):
        self.pos = pos
        self.norm = norm
        self.uv = uv
        self.col = col
        self.bones = bones # 4 uint8
        self.weights = weights # 4 float

def create_cylinder_part(p0, p1, r0, r1, segments, col, b0, b1):
    """Genera un cilindro/cápsula entre dos puntos interpolando pesos entre b0 y b1"""
    verts = []
    indices = []

    # Eje del cilindro
    dx = p1[0] - p0[0]
    dy = p1[1] - p0[1]
    dz = p1[2] - p0[2]
    length = math.sqrt(dx*dx + dy*dy + dz*dz)
    if length < 1e-5:
        return verts, indices
    axis = (dx/length, dy/length, dz/length)

    # Base ortonormal
    up = (0.0, 1.0, 0.0) if abs(axis[1]) < 0.9 else (1.0, 0.0, 0.0)
    rx = axis[1]*up[2] - axis[2]*up[1]
    ry = axis[2]*up[0] - axis[0]*up[2]
    rz = axis[0]*up[1] - axis[1]*up[0]
    rlen = math.sqrt(rx*rx + ry*ry + rz*rz)
    u_vec = (rx/rlen, ry/rlen, rz/rlen)
    v_vec = (axis[1]*u_vec[2] - axis[2]*u_vec[1],
             axis[2]*u_vec[0] - axis[0]*u_vec[2],
             axis[0]*u_vec[1] - axis[1]*u_vec[0])

    rings = 5
    for ring in range(rings):
        t = ring / (rings - 1)
        # Interpolación de posición central y radio
        cx = p0[0] + dx * t
        cy = p0[1] + dy * t
        cz = p0[2] + dz * t
        r = r0 + (r1 - r0) * t

        # Pesos de deformación suaves (Hermite smoothstep)
        w1 = t * t * (3.0 - 2.0 * t)
        w0 = 1.0 - w1
        bones = (b0, b1, 0, 0)
        weights = (w0, w1, 0.0, 0.0)

        for seg in range(segments):
            angle = (seg / segments) * 2.0 * math.pi
            cos_a = math.cos(angle)
            sin_a = math.sin(angle)

            norm = (u_vec[0]*cos_a + v_vec[0]*sin_a,
                    u_vec[1]*cos_a + v_vec[1]*sin_a,
                    u_vec[2]*cos_a + v_vec[2]*sin_a)
            pos = (cx + norm[0]*r, cy + norm[1]*r, cz + norm[2]*r)
            uv = (seg / segments, t)

            verts.append(SkinnedVertex(pos, norm, uv, col, bones, weights))

    for ring in range(rings - 1):
        for seg in range(segments):
            next_seg = (seg + 1) % segments
            i0 = ring * segments + seg
            i1 = ring * segments + next_seg
            i2 = (ring + 1) * segments + seg
            i3 = (ring + 1) * segments + next_seg

            indices.extend([i0, i1, i2])
            indices.extend([i1, i3, i2])

    return verts, indices

def create_box_part(center, extents, col, bone_idx):
    """Genera una caja asignada a un único hueso (ej. cabeza, pecho, pelvis, pies)"""
    verts = []
    indices = []
    cx, cy, cz = center
    ex, ey, ez = extents

    # 6 caras de la caja
    faces = [
        ((0, 0, 1), [(cx-ex, cy-ey, cz+ez), (cx+ex, cy-ey, cz+ez), (cx+ex, cy+ey, cz+ez), (cx-ex, cy+ey, cz+ez)]),
        ((0, 0, -1), [(cx+ex, cy-ey, cz-ez), (cx-ex, cy-ey, cz-ez), (cx-ex, cy+ey, cz-ez), (cx+ex, cy+ey, cz-ez)]),
        ((0, 1, 0), [(cx-ex, cy+ey, cz+ez), (cx+ex, cy+ey, cz+ez), (cx+ex, cy+ey, cz-ez), (cx-ex, cy+ey, cz-ez)]),
        ((0, -1, 0), [(cx-ex, cy-ey, cz-ez), (cx+ex, cy-ey, cz-ez), (cx+ex, cy-ey, cz+ez), (cx-ex, cy-ey, cz+ez)]),
        ((1, 0, 0), [(cx+ex, cy-ey, cz+ez), (cx+ex, cy-ey, cz-ez), (cx+ex, cy+ey, cz-ez), (cx+ex, cy+ey, cz+ez)]),
        ((-1, 0, 0), [(cx-ex, cy-ey, cz-ez), (cx-ex, cy-ey, cz+ez), (cx-ex, cy+ey, cz+ez), (cx-ex, cy+ey, cz-ez)]),
    ]

    uvs = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
    bones = (bone_idx, 0, 0, 0)
    weights = (1.0, 0.0, 0.0, 0.0)

    for norm, pts in faces:
        base_idx = len(verts)
        for i, pt in enumerate(pts):
            verts.append(SkinnedVertex(pt, norm, uvs[i], col, bones, weights))
        indices.extend([base_idx, base_idx + 1, base_idx + 2])
        indices.extend([base_idx, base_idx + 2, base_idx + 3])

    return verts, indices

def build_character_mesh(skeleton):
    # Coordenadas mundiales absolutas de cada articulación en bind pose
    wpos = []
    for j in skeleton:
        wpos.append((j['world_bind'][12], j['world_bind'][13], j['world_bind'][14]))

    all_verts = []
    all_indices = []

    def append_mesh(v_list, i_list):
        base_idx = len(all_verts)
        all_verts.extend(v_list)
        for idx in i_list:
            all_indices.append(base_idx + idx)

    # Paleta de colores estilizados
    col_titanium = (0.28, 0.32, 0.38, 1.0)
    col_armor    = (0.85, 0.88, 0.92, 1.0)
    col_gold     = (0.95, 0.75, 0.20, 1.0)
    col_energy   = (0.20, 0.85, 1.00, 1.0) # Ojos / visor cian brillante

    # 1. Pelvis (Caja de armadura)
    v, i = create_box_part(wpos[0], (0.28, 0.15, 0.18), col_titanium, 0)
    append_mesh(v, i)

    # 2. Columna Vertebral (Cilindro con skinning Pelvis -> Spine)
    v, i = create_cylinder_part(wpos[0], wpos[1], 0.18, 0.20, 8, col_armor, 0, 1)
    append_mesh(v, i)

    # 3. Pecho / Tórax (Caja de armadura)
    v, i = create_box_part(wpos[2], (0.38, 0.22, 0.22), col_titanium, 2)
    append_mesh(v, i)
    # Placa dorada en el pecho
    v, i = create_box_part((wpos[2][0], wpos[2][1], wpos[2][2] + 0.23), (0.16, 0.14, 0.02), col_gold, 2)
    append_mesh(v, i)

    # 4. Cuello y Cabeza
    neck_pt = (wpos[3][0], wpos[3][1] - 0.15, wpos[3][2])
    v, i = create_cylinder_part(wpos[2], neck_pt, 0.12, 0.12, 8, col_titanium, 2, 3)
    append_mesh(v, i)
    # Casco / Cabeza
    v, i = create_box_part(wpos[3], (0.20, 0.22, 0.22), col_armor, 3)
    append_mesh(v, i)
    # Visor luminoso cian (ojos cibernéticos)
    v, i = create_box_part((wpos[3][0], wpos[3][1] + 0.04, wpos[3][2] + 0.23), (0.15, 0.05, 0.02), col_energy, 3)
    append_mesh(v, i)

    # 5. Brazo Izquierdo
    # Hombro -> Codo
    v, i = create_cylinder_part(wpos[4], wpos[5], 0.12, 0.10, 8, col_armor, 4, 5)
    append_mesh(v, i)
    # Codo -> Mano
    v, i = create_cylinder_part(wpos[5], wpos[6], 0.10, 0.08, 8, col_titanium, 5, 6)
    append_mesh(v, i)
    # Mano
    v, i = create_box_part(wpos[6], (0.08, 0.06, 0.10), col_gold, 6)
    append_mesh(v, i)

    # 6. Brazo Derecho
    # Hombro -> Codo
    v, i = create_cylinder_part(wpos[7], wpos[8], 0.12, 0.10, 8, col_armor, 7, 8)
    append_mesh(v, i)
    # Codo -> Mano
    v, i = create_cylinder_part(wpos[8], wpos[9], 0.10, 0.08, 8, col_titanium, 8, 9)
    append_mesh(v, i)
    # Mano
    v, i = create_box_part(wpos[9], (0.08, 0.06, 0.10), col_gold, 9)
    append_mesh(v, i)

    # 7. Pierna Izquierda
    # Cadera -> Rodilla
    v, i = create_cylinder_part(wpos[10], wpos[11], 0.16, 0.13, 8, col_armor, 10, 11)
    append_mesh(v, i)
    # Rodilla -> Pie
    v, i = create_cylinder_part(wpos[11], wpos[12], 0.13, 0.11, 8, col_titanium, 11, 12)
    append_mesh(v, i)
    # Pie
    foot_l = (wpos[12][0], wpos[12][1] - 0.05, wpos[12][2] + 0.10)
    v, i = create_box_part(foot_l, (0.12, 0.08, 0.22), col_gold, 12)
    append_mesh(v, i)

    # 8. Pierna Derecha
    # Cadera -> Rodilla
    v, i = create_cylinder_part(wpos[13], wpos[14], 0.16, 0.13, 8, col_armor, 13, 14)
    append_mesh(v, i)
    # Rodilla -> Pie
    v, i = create_cylinder_part(wpos[14], wpos[15], 0.13, 0.11, 8, col_titanium, 14, 15)
    append_mesh(v, i)
    # Pie
    foot_r = (wpos[15][0], wpos[15][1] - 0.05, wpos[15][2] + 0.10)
    v, i = create_box_part(foot_r, (0.12, 0.08, 0.22), col_gold, 15)
    append_mesh(v, i)

    return all_verts, all_indices

# -----------------------------------------------------------------------------
# Generador de Clips de Animación (Idle, Walk, Wave_Flex)
# -----------------------------------------------------------------------------
def build_animation_clips(skeleton):
    clips = []

    # 1. Clip "Idle" (2.0 segundos, 60 fps)
    idle_dur = 2.0
    idle_frames = 60
    idle_tracks = []
    for j_idx, j in enumerate(skeleton):
        kfs = []
        lpos = (j['local_bind'][12], j['local_bind'][13], j['local_bind'][14])
        for f in range(idle_frames + 1):
            t = (f / idle_frames) * idle_dur
            phase = (t / idle_dur) * 2.0 * math.pi
            trans = lpos
            rot = quat_identity()

            # Respiración de pecho y espina
            if j_idx == 2: # Chest
                pitch = math.sin(phase) * 0.04
                rot = quat_from_axis_angle((1.0, 0.0, 0.0), pitch)
            elif j_idx == 0: # Pelvis
                dy = math.sin(phase) * 0.02
                trans = (lpos[0], lpos[1] + dy, lpos[2])
            elif j_idx == 4: # L_Shoulder
                pitch = math.cos(phase) * 0.05
                rot = quat_from_axis_angle((1.0, 0.0, 0.0), pitch)
            elif j_idx == 7: # R_Shoulder
                pitch = -math.cos(phase) * 0.05
                rot = quat_from_axis_angle((1.0, 0.0, 0.0), pitch)

            kfs.append({'time': t, 'trans': trans, 'rot': rot, 'scale': (1.0, 1.0, 1.0)})
        idle_tracks.append(kfs)
    clips.append({'name': 'Idle', 'duration': idle_dur, 'fps': 30.0, 'loop': True, 'tracks': idle_tracks})

    # 2. Clip "Walk" (1.2 segundos, 60 fps)
    walk_dur = 1.2
    walk_frames = 60
    walk_tracks = []
    for j_idx, j in enumerate(skeleton):
        kfs = []
        lpos = (j['local_bind'][12], j['local_bind'][13], j['local_bind'][14])
        for f in range(walk_frames + 1):
            t = (f / walk_frames) * walk_dur
            phase = (t / walk_dur) * 2.0 * math.pi
            trans = lpos
            rot = quat_identity()

            # Marcha de piernas contrapuestas
            if j_idx == 10: # L_Hip
                pitch = math.sin(phase) * 0.45
                rot = quat_from_axis_angle((1.0, 0.0, 0.0), pitch)
            elif j_idx == 11: # L_Knee
                pitch = max(0.0, -math.sin(phase) * 0.65)
                rot = quat_from_axis_angle((1.0, 0.0, 0.0), pitch)
            elif j_idx == 13: # R_Hip
                pitch = -math.sin(phase) * 0.45
                rot = quat_from_axis_angle((1.0, 0.0, 0.0), pitch)
            elif j_idx == 14: # R_Knee
                pitch = max(0.0, math.sin(phase) * 0.65)
                rot = quat_from_axis_angle((1.0, 0.0, 0.0), pitch)
            elif j_idx == 4: # L_Shoulder (balanceo opuesto a pierna izq)
                pitch = -math.sin(phase) * 0.35
                rot = quat_from_axis_angle((1.0, 0.0, 0.0), pitch)
            elif j_idx == 7: # R_Shoulder
                pitch = math.sin(phase) * 0.35
                rot = quat_from_axis_angle((1.0, 0.0, 0.0), pitch)
            elif j_idx == 0: # Pelvis bob
                dy = abs(math.sin(phase * 2.0)) * 0.04
                trans = (lpos[0], lpos[1] + dy, lpos[2])

            kfs.append({'time': t, 'trans': trans, 'rot': rot, 'scale': (1.0, 1.0, 1.0)})
        walk_tracks.append(kfs)
    clips.append({'name': 'Walk', 'duration': walk_dur, 'fps': 30.0, 'loop': True, 'tracks': walk_tracks})

    # 3. Clip "Wave_Flex" (2.5 segundos, 75 fps)
    wave_dur = 2.5
    wave_frames = 75
    wave_tracks = []
    for j_idx, j in enumerate(skeleton):
        kfs = []
        lpos = (j['local_bind'][12], j['local_bind'][13], j['local_bind'][14])
        for f in range(wave_frames + 1):
            t = (f / wave_frames) * wave_dur
            norm_t = t / wave_dur
            trans = lpos
            rot = quat_identity()

            # Brazo izquierdo se eleva, flexiona y saluda
            if j_idx == 4: # L_Shoulder eleva
                roll = math.sin(norm_t * math.pi) * 1.85 # Eleva hacia arriba en Z
                rot = quat_from_axis_angle((0.0, 0.0, 1.0), roll)
            elif j_idx == 5: # L_Elbow flexiona
                pitch = math.sin(norm_t * math.pi) * 1.25 # Dobla 70 grados
                rot = quat_from_axis_angle((1.0, 0.0, 0.0), pitch)
            elif j_idx == 6: # L_Hand saluda
                wave = math.sin(norm_t * math.pi * 8.0) * 0.45
                rot = quat_from_axis_angle((0.0, 1.0, 0.0), wave)
            elif j_idx == 3: # Head mira a la mano
                yaw = math.sin(norm_t * math.pi) * 0.25
                rot = quat_from_axis_angle((0.0, 1.0, 0.0), yaw)

            kfs.append({'time': t, 'trans': trans, 'rot': rot, 'scale': (1.0, 1.0, 1.0)})
        wave_tracks.append(kfs)
    clips.append({'name': 'Wave_Flex', 'duration': wave_dur, 'fps': 30.0, 'loop': True, 'tracks': wave_tracks})

    return clips

# -----------------------------------------------------------------------------
# Serialización a Archivos Binarios Big-Endian
# -----------------------------------------------------------------------------
def export_files():
    skeleton = build_skeleton()
    verts, indices = build_character_mesh(skeleton)
    clips = build_animation_clips(skeleton)

    os.makedirs("assets/models", exist_ok=True)

    # 1. Exportar assets/models/character.wmesh
    wmesh_path = "assets/models/character.wmesh"
    flags = MESH_FLAG_SKINNED | MESH_FLAG_HAS_COLOR | MESH_FLAG_HAS_TEXCOORD | MESH_FLAG_HAS_NORMAL | MESH_FLAG_BIG_ENDIAN

    vertex_count = len(verts)
    index_count = len(indices)
    vbo_size = vertex_count * SKINNED_VERTEX_STRIDE
    vbo_offset = 256 # Alineado a 256 bytes

    raw_ibo_offset = vbo_offset + vbo_size
    ibo_offset = (raw_ibo_offset + 31) & ~31
    ibo_size = index_count * 2 # uint16_t

    # Bounding Box y Esfera envolvente
    xs = [v.pos[0] for v in verts]
    ys = [v.pos[1] for v in verts]
    zs = [v.pos[2] for v in verts]
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)
    min_z, max_z = min(zs), max(zs)
    cx, cy, cz = (min_x + max_x) * 0.5, (min_y + max_y) * 0.5, (min_z + max_z) * 0.5
    radius = max(math.sqrt((v.pos[0]-cx)**2 + (v.pos[1]-cy)**2 + (v.pos[2]-cz)**2) for v in verts)

    header = struct.pack(
        '>4sHHIIIIIIII4f3f3fI3I',
        WMESH_MAGIC,
        1, # Version
        flags,
        vertex_count,
        index_count,
        SKINNED_VERTEX_STRIDE,
        0, # Index format 0 = U16
        vbo_offset,
        vbo_size,
        ibo_offset,
        ibo_size,
        cx, cy, cz, radius,
        min_x, min_y, min_z,
        max_x, max_y, max_z,
        1, # Submesh count
        0, 0, 0
    )

    with open(wmesh_path, 'wb') as f:
        f.write(header)
        f.write(b'\x00' * (vbo_offset - f.tell()))

        # VBO: pos 3f, norm 3f, uv 2f, col 4f, bones 4B, weights 4f, pad 3f (80 bytes)
        for v in verts:
            v_bytes = struct.pack(
                '>3f3f2f4f4B4f3f',
                v.pos[0], v.pos[1], v.pos[2],
                v.norm[0], v.norm[1], v.norm[2],
                v.uv[0], v.uv[1],
                v.col[0], v.col[1], v.col[2], v.col[3],
                v.bones[0], v.bones[1], v.bones[2], v.bones[3],
                v.weights[0], v.weights[1], v.weights[2], v.weights[3],
                0.0, 0.0, 0.0
            )
            f.write(v_bytes)

        f.write(b'\x00' * (ibo_offset - f.tell()))
        for idx in indices:
            f.write(struct.pack('>H', idx))

    print(f"Exported '{wmesh_path}': {vertex_count} verts ({vbo_size}B), {index_count} indices ({ibo_size}B)")

    # 2. Exportar assets/models/character.wskel
    wskel_path = "assets/models/character.wskel"
    joint_count = len(skeleton)
    with open(wskel_path, 'wb') as f:
        # Header: magic (4s), version (H), jointCount (H), reserved (I), pad (20s) = 32 bytes
        f.write(struct.pack('>4sHHI20s', WSKL_MAGIC, 1, joint_count, 0, b'\x00'*20))

        for j in skeleton:
            # name (32s), parent (h), reserved (h), invBind (16f), localBind (16f) = 164 bytes
            name_bytes = j['name'].encode('ascii')[:31].ljust(32, b'\x00')
            inv_bind = j['inv_bind']
            local_bind = j['local_bind']
            joint_bytes = struct.pack(
                '>32shh16f16f',
                name_bytes,
                j['parent'],
                0,
                *inv_bind,
                *local_bind
            )
            f.write(joint_bytes)

    print(f"Exported '{wskel_path}': {joint_count} joints")

    # 3. Exportar assets/models/character.wanim
    wanim_path = "assets/models/character.wanim"
    clip_count = len(clips)
    with open(wanim_path, 'wb') as f:
        # Header: magic (4s), version (H), clipCount (H), reserved (I), pad (20s) = 32 bytes
        f.write(struct.pack('>4sHHI20s', WANM_MAGIC, 1, clip_count, 0, b'\x00'*20))

        for c in clips:
            name_bytes = c['name'].encode('ascii')[:31].ljust(32, b'\x00')
            is_loop = 1 if c['loop'] else 0
            f.write(struct.pack('>32sffII', name_bytes, c['duration'], c['fps'], is_loop, joint_count))

            for track in c['tracks']:
                kf_count = len(track)
                f.write(struct.pack('>I', kf_count))
                for kf in track:
                    # time (f), trans (3f), rot (4f), scale (3f) = 11 floats (44 bytes)
                    f.write(struct.pack(
                        '>f3f4f3f',
                        kf['time'],
                        kf['trans'][0], kf['trans'][1], kf['trans'][2],
                        kf['rot'][0], kf['rot'][1], kf['rot'][2], kf['rot'][3],
                        kf['scale'][0], kf['scale'][1], kf['scale'][2]
                    ))

    print(f"Exported '{wanim_path}': {clip_count} animation clips ({', '.join(c['name'] for c in clips)})")

if __name__ == '__main__':
    export_files()
