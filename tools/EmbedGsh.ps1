[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("native_gx2", "raytracing", "raytracing_bvh", "fullscreen_blit", "water", "shadows_gx2", "environment_gx2", "skinning_gx2", "pbr_mesh_gx2")]
    [string] $Shader
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$shaderFiles = @{
    native_gx2 = @{
        Binary = "assets/shaders/native_gx2.gsh"
        Header = "include/Graphics/NativeGX2ShaderBinary.hpp"
        SizeSymbol = "NATIVE_GX2_GSH_SIZE"
        DataSymbol = "NATIVE_GX2_GSH_DATA"
    }
    fullscreen_blit = @{
        Binary = "assets/shaders/fullscreen_blit.gsh"
        Header = "include/Graphics/FullscreenBlitShaderBinary.hpp"
        SizeSymbol = "FULLSCREEN_BLIT_GSH_SIZE"
        DataSymbol = "FULLSCREEN_BLIT_GSH_DATA"
    }
    raytracing = @{
        Binary = "assets/shaders/raytracing.gsh"
        Header = "include/Graphics/RaytracingShaderBinary.hpp"
        SizeSymbol = "RAYTRACING_GSH_SIZE"
        DataSymbol = "RAYTRACING_GSH_DATA"
    }
    raytracing_bvh = @{
        Binary = "assets/shaders/raytracing_bvh.gsh"
        Header = "include/Graphics/RaytracingBVHShaderBinary.hpp"
        SizeSymbol = "RAYTRACING_BVH_GSH_SIZE"
        DataSymbol = "RAYTRACING_BVH_GSH_DATA"
    }
    water = @{
        Binary = "assets/shaders/water.gsh"
        Header = "include/Graphics/WaterShaderBinary.hpp"
        SizeSymbol = "WATER_GSH_SIZE"
        DataSymbol = "WATER_GSH_DATA"
    }
    shadows_gx2 = @{
        Binary = "assets/shaders/shadows_gx2.gsh"
        Header = "include/Graphics/ShadowsShaderBinary.hpp"
        SizeSymbol = "SHADOWS_GX2_GSH_SIZE"
        DataSymbol = "SHADOWS_GX2_GSH_DATA"
    }
    environment_gx2 = @{
        Binary = "assets/shaders/environment_gx2.gsh"
        Header = "include/Graphics/EnvironmentShaderBinary.hpp"
        SizeSymbol = "ENVIRONMENT_GX2_GSH_SIZE"
        DataSymbol = "ENVIRONMENT_GX2_GSH_DATA"
    }
    skinning_gx2 = @{
        Binary = "assets/shaders/skinning_gx2.gsh"
        Header = "include/Graphics/SkinningShaderBinary.hpp"
        SizeSymbol = "SKINNING_GX2_GSH_SIZE"
        DataSymbol = "SKINNING_GX2_GSH_DATA"
    }
    pbr_mesh_gx2 = @{
        Binary = "assets/shaders/pbr_mesh_gx2.gsh"
        Header = "include/Graphics/PBRMeshShaderBinary.hpp"
        SizeSymbol = "PBR_MESH_GX2_GSH_SIZE"
        DataSymbol = "PBR_MESH_GX2_GSH_DATA"
    }
}

$shaderInfo = $shaderFiles[$Shader]
$binaryPath = Join-Path $projectRoot $shaderInfo.Binary
$headerPath = Join-Path $projectRoot $shaderInfo.Header
if (-not (Test-Path -LiteralPath $binaryPath -PathType Leaf)) {
    throw "Compiled shader not found: $binaryPath. Compile its .vert/.frag pair first."
}

$bytes = [System.IO.File]::ReadAllBytes($binaryPath)
if ($bytes.Length -lt 4 -or $bytes[0] -ne 0x47 -or $bytes[1] -ne 0x66 -or $bytes[2] -ne 0x78 -or $bytes[3] -ne 0x32) {
    throw "Input is not a GFD shader binary (expected Gfx2 header): $binaryPath"
}

$text = [System.Text.StringBuilder]::new()
[void] $text.AppendLine("#pragma once")
[void] $text.AppendLine()
[void] $text.AppendLine("// Fallback generado desde $($shaderInfo.Binary) por tools/EmbedGsh.ps1.")
[void] $text.AppendLine("#include <cstddef>")
[void] $text.AppendLine("#include <cstdint>")
[void] $text.AppendLine()
[void] $text.AppendLine("namespace WiiU {")
[void] $text.AppendLine("inline constexpr size_t $($shaderInfo.SizeSymbol) = $($bytes.Length);")
[void] $text.AppendLine("inline const uint8_t $($shaderInfo.DataSymbol)[$($bytes.Length)] = {")

for ($offset = 0; $offset -lt $bytes.Length; $offset += 16) {
    $end = [Math]::Min($offset + 16, $bytes.Length)
    $values = for ($index = $offset; $index -lt $end; $index++) {
        "0x{0:x2}" -f $bytes[$index]
    }
    [void] $text.AppendLine("    $($values -join ', '),")
}

[void] $text.AppendLine("};")
[void] $text.AppendLine("} // namespace WiiU")

$parentDirectory = Split-Path -Parent $headerPath
if (-not (Test-Path -LiteralPath $parentDirectory -PathType Container)) {
    [void] [System.IO.Directory]::CreateDirectory($parentDirectory)
}

$temporaryPath = "$headerPath.$([guid]::NewGuid().ToString('N')).tmp"
try {
    [System.IO.File]::WriteAllText($temporaryPath, $text.ToString(), [System.Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temporaryPath -Destination $headerPath -Force -ErrorAction Stop
} finally {
    if (Test-Path -LiteralPath $temporaryPath -PathType Leaf) {
        Remove-Item -LiteralPath $temporaryPath
    }
}
Write-Output "Embedded $($bytes.Length) bytes from $($shaderInfo.Binary) into $headerPath"