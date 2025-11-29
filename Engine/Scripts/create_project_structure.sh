#!/usr/bin/env bash
set -e
ROOT="${1:-./Engine}"
echo "Creating engine skeleton at $ROOT"
# minimal example: create a few directories (the real script would loop the full list)
dirs=(
"Source/Core/Allocators"
"Source/Rendering/RHI/Vulkan"
"Source/Physics/Broadphase"
"Content/Textures/Environment/Terrain"
"Pipeline/Render/Illumination/GlobalIllumination/VoxelGI"
"Config"
"Build/toolchains"
"Binaries/Win64"
"Scripts"
)
for d in "${dirs[@]}"; do
  mkdir -p "$ROOT/$d"
done
echo "Created $(printf '%s\n' "${dirs[@]}" | wc -l) folders."