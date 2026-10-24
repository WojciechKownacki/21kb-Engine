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
