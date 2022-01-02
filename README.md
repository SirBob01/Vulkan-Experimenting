# Vulkan-Experimenting

A small repository containing my experiments with Vulkan.

The final renderer will be integrated into my `Dynamo` Engine.

# TODO
- Implement descriptors for per-object data (transforms, lighting variables, etc.)
- Implement threaded secondary command buffer recording for mesh rendering
- Implement interface for standard primitive rendering functions like `draw_rect`, `draw_circle`, `draw_line`, `draw_cube`, `draw_text`, and `draw_mesh`. This will involve recording commands that manipulate dynamic state, as well as switching between different graphics pipelines
- Look into pipeline caches
- Figure out how to abstract render pass creation (if possible)