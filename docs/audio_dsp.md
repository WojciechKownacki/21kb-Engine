# Audio DSP primitives

The miniaudio provider contains a private filter-chain primitive for ordered
low-pass and high-pass stages. It builds actual native graph nodes with the
vendored miniaudio API; the dependency's existing license and notices remain
in its source tree. Repeated stages keep separate parameters and state.
Bypassed stages retain validated settings and create no processing node.

The primitive accepts 1–8 channels, sample rates from 8 to 192 kHz, at most
16 stages, filter orders 1–8, and finite cutoff frequencies from 1 Hz through
49% of the sample rate. Invalid parameters and failed construction preserve the
previous graph and signal. Partial node initialization also releases filter
storage when the graph-node allocation fails. Allocator callbacks are copied;
their user data must remain alive until the nodes are reset.

The graph and attached source/target outlive the chain. Configuration and graph
attachment run on the owner thread outside concurrent PCM reads. Successful
construction builds unattached nodes; callers attach the new chain before
reading it. Reset releases owned filter nodes. An empty or fully bypassed chain
attaches the source directly to the target.

`kb_audio_filter_tests` reads real generated sine waves through the node graph.
Eighteen response cases check empty/bypassed, repeated and mixed stages, both
orders of a mixed pair, mono/stereo/eight-channel output, sample-rate boundaries
and sixteen active stages. Four 4800-frame blocks per case must match an
independent second-order Butterworth response within 1%. Additional tests reject
invalid settings and attachments, exercise orders 1–8, and verify that rejected
edits preserve the playing signal. Every observed allocation in a three-stage
replacement is separately refused; the old chain must keep its exact live
allocation count and requested bytes, then release them all on reset.

The tests pass in x64 MSVC Release and Debug ASan, and in a 32-bit WebAssembly
ASan/UBSan build of the same sources. The WebAssembly check uses the real
graph/filter/waveform implementation with device I/O, decoders and threading
omitted. One stereo second-order stage requested 7760 bytes from the vendor
allocator on x64 and 7744 bytes in WebAssembly. These figures exclude C++ node
objects, container storage, source/output buffers and sanitizer overhead.

Mixer asset serialization, editor controls, CLI authoring, cooking, packaged
bus attachment and concurrent runtime parameter changes remain pending. These
primitives alone do not qualify bus DSP effects as an end-to-end feature.
