# The Material Instance editor

Double-click a material instance in the asset browser (or `OpenResourceEditor -Asset <guid>`): the material it is made from, its textures with a
thumbnail each, and a 3D preview of the compiled instance on a mesh. Everything the window changes goes through commands on the editor's own
undo system.

```
source/Editor/xmaterial_instance_editor.h     the editor (session), SetMaterial, its registration
```

The generic half (document, `SetProperty`, `Save`, `Compile`, `Undo`, the window and its dock space) is `xeditor::descriptor_editor`; the mesh
preview is `xeditor::mesh_preview` and the texture thumbnails `xeditor::texture_thumbnails` (both in `source/Tools/Editor/`). A host includes
`xmaterial_instance_editor.h`, which registers the editor and compiles the resource loader into the host.

## Commands

Run as `<resource name>\<Command>`. Paths and values are base64.

| Command | |
|---|---|
| `SetMaterial -Material hexguid [-Before hexguid]` | makes the instance from another (compiled) material; its textures become the defaults (undoable) |
| `ListProperties [-Filter]`, `SetProperty -Path -Value [-Before]`, `SnapshotEdit` | descriptor properties (undoable); a texture slot is `.../Textures[G:n]` |
| `Save`, `Compile`, `Undo`, `Redo` | |
| `CompileStatus [-Lines n]` | how the last compile went: state, unsaved changes, validation errors, the end of the log |
| `SetPreviewMesh [-Model name]` | the mesh the instance is shown on (no name lists them) |
| `SetCamera [-Yaw -Pitch -Distance -Target x,y,z]`, `GetCamera`, `FrameSubject` | the preview camera (degrees), read back, or refitted to the subject (view state, not undoable) |
