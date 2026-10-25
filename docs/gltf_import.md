# glTF import limits

The shared glTF loader checks accessor dimensions before loading buffers or
allocating decoded arrays. Accessor element counts must be positive and their
types must have a valid component count. All accessors in one document share a
2 GiB budget, measured as `element count * component count * sizeof(float)`.
The check divides before multiplying to avoid integer overflow. It reports the
first accessor that exceeds the remaining budget.

This accounts for zero-initialized accessors without a buffer view and for the
sum of multiple accessors. A tiny JSON file can otherwise request a large
decoded array. Valid accessors without backing data retain their specified
zero initialization. The budget is a size limit, not an allocation reserved
when import starts; actual memory also includes meshes, materials and buffers.

CLI and editor import use the same check when collecting source resources.
Runtime static and skeletal glTF loaders check it before decoding. An oversized
document fails import with a diagnostic instead of creating an asset that
later throws during mesh loading. Split large source documents when the
diagnostic identifies this limit.

`kb_renderer_tests gltf-external` covers oversized counts, the last accepted
VEC3 count and the next refused count, combined accessor sizes, zero counts and
valid zero initialization. It also retains the external-resource, material and
import-transform regressions. The mesh glTF fuzz corpus includes an oversized
accessor regression for normal corpus replay.

Import resource collection and shared buffer loading also preflight the selected
scene before loading buffers or allocating expanded vertex arrays. A separate 2 GiB
working-size estimate includes both vertex formats, a tangent-format copy and
vertex-remap workspace, both index widths, mesh sections, and the largest
primitive's decoded attribute arrays. Repeated node instances contribute their
expanded geometry each time. Arithmetic divides before multiplying and checks
the renderer's 32-bit index range. Hierarchies are limited to 512 levels and one
million expanded node visits; preflight traverses them iteratively. The common
policy charges 256 bytes per vertex, 6 per index, and 128 per section, plus decoded
attributes. Compile-time checks keep the static renderer's layouts within those
charges. CLI, editor import, static runtime and skeletal loaders share this check.

This estimate is conservative and does not reserve memory or guarantee physical
memory availability. Materials, images, parser data, and unrelated resident
assets have separate costs. A refusal reports the working-size or hierarchy
limit before the import writes an asset. `kb_renderer_tests gltf-external`
checks metadata-only accepted sizes, oversized output, repeated instances, and
the actual previously failing numeric-token case through runtime loading and
import inspection. The corpus includes a valid numeric oversized mesh variant.
