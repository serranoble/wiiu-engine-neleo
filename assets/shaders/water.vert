// Deforma la malla de agua con cuatro ondas Gerstner y proyecta en espacio de cámara.
// El código C++ carga 11 uniforms vec4 en KC2[0..10]; conserva ese contrato al editar.
#version 330 core
layout(location = 0) in vec2 aPos;

// Uniforms de Cámara y Proyección (KC2[0 .. 4])
uniform vec4 uCamPos;      // KC2[0]: xyz = camPos, w = 0
uniform vec4 uCamForward;  // KC2[1]: xyz = camForward, w = 0
uniform vec4 uCamRight;    // KC2[2]: xyz = camRight, w = 0
uniform vec4 uCamUp;       // KC2[3]: xyz = camUp, w = 0
uniform vec4 uFocal;       // KC2[4]: x = focalX, y = focalY, zw = 0

// Ondas Trocoidales de Gerstner (KC2[5 .. 8])
uniform vec4 uWave0;       // KC2[5]: x=dirX, y=dirZ, z=amplitude, w=wavelength
uniform vec4 uWave1;       // KC2[6]: x=dirX, y=dirZ, z=amplitude, w=wavelength
uniform vec4 uWave2;       // KC2[7]: x=dirX, y=dirZ, z=amplitude, w=wavelength
uniform vec4 uWave3;       // KC2[8]: x=dirX, y=dirZ, z=amplitude, w=wavelength

// Parámetros de Simulación y Velocidades (KC2[9 .. 10])
uniform vec4 uSimParams;   // KC2[9]: x=time, y=waterLevel, z=ampMultiplier, w=numWaves
uniform vec4 uWaveSpeeds;  // KC2[10]: x=speed0, y=speed1, z=speed2, w=speed3

out vec3 vWorldPos;
out vec3 vNormal;

void evaluateWave(vec4 wave, float speed, float time, float ampMult, vec2 p0,
                  inout vec3 pos, inout vec3 normal) {
    float amp = wave.z * ampMult;
    if (amp <= 0.0001) return;
    
    float k = 6.2831853 / wave.w;
    float wSpeed = sqrt(9.81 * k) * speed;
    float q = min(1.0, 0.65 / (k * amp * 4.0));
    
    float phase = (wave.x * p0.x + wave.y * p0.y) * k + time * wSpeed;
    float cosP = cos(phase);
    float sinP = sin(phase);
    
    pos.x += q * amp * wave.x * cosP;
    pos.y += amp * sinP;
    pos.z += q * amp * wave.y * cosP;
    
    normal.x -= wave.x * (k * amp) * cosP;
    normal.y -= q * (k * amp) * sinP;
    normal.z -= wave.y * (k * amp) * cosP;
}

void main() {
    vec2 p0 = aPos;
    vec3 pos = vec3(p0.x, uSimParams.y, p0.y);
    vec3 normal = vec3(0.0, 1.0, 0.0);
    
    float time = uSimParams.x;
    float ampMult = uSimParams.z;
    
    evaluateWave(uWave0, uWaveSpeeds.x, time, ampMult, p0, pos, normal);
    evaluateWave(uWave1, uWaveSpeeds.y, time, ampMult, p0, pos, normal);
    evaluateWave(uWave2, uWaveSpeeds.z, time, ampMult, p0, pos, normal);
    evaluateWave(uWave3, uWaveSpeeds.w, time, ampMult, p0, pos, normal);
    
    vWorldPos = pos;
    vNormal = normal;

    // Proyección en perspectiva 3D al espacio de cámara (1:1 con la CPU)
    vec3 rel = pos - uCamPos.xyz;
    float cz = dot(rel, uCamForward.xyz);
    float cx = dot(rel, uCamRight.xyz);
    float cy = dot(rel, uCamUp.xyz);
    
    // Mapeo canónico a clip space (z normalizado en rango visible)
    gl_Position = vec4(cx * uFocal.x, cy * uFocal.y, cz * 0.99 - 0.5, cz);
}
