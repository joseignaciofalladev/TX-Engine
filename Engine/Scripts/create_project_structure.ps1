param($Root = ".\Engine")
Write-Host "Creating engine skeleton at $Root"
$dirs = @(
"Source\Core\Allocators",
"Source\Rendering\RHI\Vulkan",
"Source\Physics\Broadphase",
"Content\Textures\Environment\Terrain",
"Pipeline\Render\Illumination\GlobalIllumination\VoxelGI",
"Config",
"Build\toolchains",
"Binaries\Win64",
"Scripts"
)

foreach ($d in $dirs) {
  $p = Join-Path $Root $d
  New-Item -ItemType Directory -Force -Path $p | Out-Null
}

Write-Host "Done."
