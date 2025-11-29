#!/usr/bin/env python3
import os, sys
root = sys.argv[1] if len(sys.argv)>1 else "Engine"
dirs = [
"Source/Core/Allocators",
"Source/Rendering/RHI/Vulkan",
"Source/Physics/Broadphase",
"Content/Textures/Environment/Terrain",
"Pipeline/Render/Illumination/GlobalIllumination/VoxelGI",
"Config",
"Build/toolchains",
"Binaries/Win64",
"Scripts"
]
for d in dirs:
    path = os.path.join(root, d)
    os.makedirs(path, exist_ok=True)
print(f"Created {len(dirs)} directories under {root}")