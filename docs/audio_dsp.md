# Audio DSP primitives

The miniaudio provider contains a private filter-chain primitive for ordered
low-pass, high-pass, peaking equalizer and delay stages. It builds actual native graph nodes with the
vendored miniaudio API; the dependency's existing license and notices remain
in its source tree. Repeated stages keep separate parameters and state.
Bypassed stages retain validated settings and create no processing node.

The primitive accepts 1–8 channels, sample rates from 8 to 192 kHz, at most
16 stages, low/high-pass orders 1–8, and finite cutoff/center frequencies from 1 Hz
through 49% of the sample rate. Peaking EQ is second order, with gain from -24 to
+24 dB and Q from 0.1 through 16. Gain and Q apply only to EQ; other kinds refuse
nondefault values for these fields. Each stage also checks the poles of its actual
quantized `float32` coefficients. Parameter combinations that become unstable
after conversion are refused, including an eighth-order 1 Hz filter at 192 kHz.
EQ boost requires input headroom; floating-point output can exceed 1.0.

Delay uses a separate dry path and a delayed feedback path with independent
state per channel. Its parameters are finite time greater than zero through
2000 ms, feedback from 0 through 0.95, and wet/dry gains from 0 through 1.
Time rounds to the nearest sample, with a minimum of one sample. Defaults are
250 ms, zero feedback, wet 0.5 and dry 1. The recurrence is
`echo[n] = input[n-D] + feedback * echo[n-D]`, with zero initial state, and
`output[n] = dry * input[n] + wet * echo[n]`. Feedback never moves the first tap
to time zero. Delay tails continue after the input reaches its end.
Unused parameters must retain their defaults for each effect kind.

Active delay lines share a 32 MiB budget per chain, checked before construction.
The charge is `roundedFrames * channels * sizeof(float)`; bypassed stages do
not allocate a line. Constructing a replacement can temporarily retain both
old and candidate lines, so their combined bound is 64 MiB plus node/container
overhead. Delay is not a limiter: its worst-case gain for a bounded input is
`dry + wet / (1 - feedback)`, multiplied across repeated stages. Callers must
reserve input headroom, including any EQ boost.

Invalid parameters and failed construction preserve the
previous graph and signal. Partial node initialization also releases filter
storage when the graph-node allocation fails. Allocator callbacks are copied;
their user data must remain alive until the nodes are reset.

The graph and attached source/target outlive the chain. Configuration and graph
attachment run on the owner thread outside concurrent PCM reads. Successful
construction builds unattached nodes; callers attach the new chain before
reading it. Reset releases owned filter nodes. An empty or fully bypassed chain
attaches the source directly to the target.

`kb_audio_filter_tests` reads real generated sine waves through the node graph.
Sixty-seven response cases check empty/bypassed, repeated and mixed stages, both
orders of a mixed pair, mono/stereo/eight-channel output, sample-rate boundaries
and sixteen active stages. Four 4800-frame blocks per case must match an
independent Butterworth or peaking transfer-function response within 1%. EQ cases
cover boost/cut, Q boundaries, repeated boosts, inverse boost/cut pairs and mixed
filter/EQ chains. Input amplitude reserves headroom for each expected gain.
Additional tests reject
invalid settings and attachments, exercise orders 1–8, and verify that rejected
edits preserve the playing signal. Twenty-four boundary combinations must stay
bounded or refuse unstable coefficients while keeping the previous graph. The
known 1 Hz instability also compares 32 PCM blocks against an independently
running unchanged graph and requires zero leaked candidate allocations.
Every observed allocation in a three-stage
replacement is separately refused with both a low-pass and EQ chain already
playing; the old chain must keep its exact live
allocation count and requested bytes, then release them all on reset.

`kb_audio_delay_tests` reads actual graph PCM and compares every sample to an
independent double-precision time-offset recurrence within an absolute 2e-6.
Twenty-five impulse cases cover tails after a one-frame source ends, separate
channels, wet-only and silent output, bypass, repeated delays, sixteen active
stages, feedback boundaries, 8–192 kHz and one through eight channels. Five read
block sizes exercise internal buffering. Six first-tap cases check rounding.
Eight continuous step/tone cases also check mixed low/high-pass, EQ and delay
chains in both orders against independently calculated filter recurrences.
Twenty-four invalid configurations and an over-budget chain must fail before
allocating or changing the old signal. Every one of ten observed vendor
allocation positions in a mixed replacement is refused with either a filter
or a delay already playing; its PCM, live allocations and requested bytes must
remain unchanged and reset must release all owned allocations.

The tests pass in x64 MSVC Release and Debug ASan, and in a 32-bit WebAssembly
ASan/UBSan build of the same sources. The WebAssembly check uses the real
graph/filter/delay/waveform implementation with device I/O, decoders and threading
omitted. One stereo second-order low/high-pass stage requested 7760 bytes from the vendor
allocator on x64 and 7744 bytes in WebAssembly. A stereo peaking EQ stage requested
7696 bytes on both tested architectures. A stereo 5 ms delay at 48 kHz requested
28800 bytes on both tested architectures, including its split/merge
graph nodes and 1920-byte delay line. These figures exclude C++ node
objects, container storage, source/output buffers and sanitizer overhead.

Mixer asset serialization, editor controls, CLI authoring, cooking, packaged
bus attachment and concurrent runtime parameter changes remain pending. These
primitives alone do not qualify bus DSP effects as an end-to-end feature.
