# Fixed Blacklisted Presets

This folder contains presets that previously caused renderer freezes on some systems.

The fixes are intentionally conservative:

- removed custom `warp_` and `comp_` shader lines from affected presets
- capped unsupported pixel shader headers where needed
- disabled textured custom shapes that depended on those shader paths
