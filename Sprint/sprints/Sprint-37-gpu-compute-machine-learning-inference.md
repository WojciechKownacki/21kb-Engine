# Sprint 37 · GPU Compute & Machine Learning Inference

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Deliver a capability-gated GPU compute framework and versioned machine-learning inference runtime with explicit scheduling, resource ownership, batching, fallback behavior, profiling, and deployment constraints.

## GPU Compute Framework

- [ ] Add a general-purpose GPU compute dispatch API
- [ ] Add compute shaders authored and compiled per backend
- [ ] Add structured, raw, and typed compute buffers
- [ ] Add read, write, and read-write resource binding
- [ ] Add indirect dispatch from GPU-generated arguments
- [ ] Add shared-memory and workgroup configuration
- [ ] Add atomic operations and counters
- [ ] Add async compute queues alongside graphics
- [ ] Add barriers and resource-state transitions for compute
- [ ] Add readback of compute results to the CPU
- [ ] Add a capability check and CPU fallback
- [ ] Add compute profiling and diagnostics
- [ ] Add a scripting and gameplay API for compute jobs

## GPGPU Utilities

- [ ] Add parallel prefix-sum (scan)
- [ ] Add GPU sorting (radix and bitonic)
- [ ] Add parallel reduction
- [ ] Add stream compaction
- [ ] Add histogram generation
- [ ] Add matrix and vector math kernels
- [ ] Add image-processing kernels (blur, convolution, resample)
- [ ] Add noise and procedural-generation kernels
- [ ] Add a reusable compute-kernel library

## ML Inference Runtime

- [ ] Add a neural-network inference runtime
- [ ] Add import of models from an open interchange format
- [ ] Add a tensor type with shapes and data types
- [ ] Add a broad operator set for common model layers
- [ ] Add CPU inference with SIMD acceleration
- [ ] Add GPU inference via compute shaders
- [ ] Add hardware neural-accelerator use where available
- [ ] Add batching and streaming inference
- [ ] Add quantized and reduced-precision inference
- [ ] Add model loading, caching, and hot-swap
- [ ] Add async inference off the main thread
- [ ] Add memory and cost budgets for inference
- [ ] Add deterministic inference for tests
- [ ] Add inference diagnostics and profiling

## ML Integration & Use Cases

- [ ] Add inference-driven animation and deformation
- [ ] Add inference-driven behavior and decision making
- [ ] Add upscaling and denoising via learned models
- [ ] Add procedural content synthesis via models
- [ ] Add speech, text, and vision model integration
- [ ] Add a training and data-capture workflow
- [ ] Add model versioning and validation
- [ ] Add safety, fallback, and reproducibility controls
- [ ] Add ML-integration tests
