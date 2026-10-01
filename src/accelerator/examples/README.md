# Accelerator examples

These examples keep application code in ordinary C++ and CUDA authoring in a small `.cu` backend.

## File structure

Each example has three parts:

- the `.hpp` file declares the typed algorithm and its owned operations and buffers;
- the `.cpp` file implements ordinary host-side processing and the example program;
- the `.cu` file builds the operations directly from execution patterns and device lambdas.

Only the `.cu` implementation contains CUDA syntax. Private, explicitly typed functions create
providers because NVCC does not permit extended device lambdas inside constructors or functions
with deduced return types. These functions have no header declaration and transfer ownership
directly into `operation_c`. There is no public provider pointer or operation identifier;
application code calls the typed `process()` method.

## Vector pipeline

`vector_pipeline` demonstrates the complete lifecycle:

1. derive an algorithm class from `accelerator_c`;
2. construct operations directly from CUDA execution-pattern factories;
3. allocate typed buffers once;
4. upload a host vector;
5. run a captured-scale `parallel_for`;
6. sum the scaled values with `transform_reduce`;
7. download the output and scalar result.

The CUDA backend declares rank-one read and write accessors. The application never supplies CUDA
thread counts, block counts, indices, streams, or synchronization primitives.

## Point-cloud centroid

`point_cloud_centroid` treats its input as an `{N, 3}` tensor and creates one logical work item per
point with `leading_dimensions(0U, 1U)`. Its transform lambda converts each point into a
`point_sum_s`; its reducer combines those structures.

ICP can use the same structure with a larger accumulator containing residual error, gradient, and
Hessian terms. Correspondence search remains a separate point-cloud operation.

## Image box blur

`image_box_blur` treats its input as an `{height, width, channels}` tensor.
`leading_dimensions(0U, 2U)` creates one logical work item per pixel. Each work item applies a 3 by
3 neighborhood filter independently to every channel. Border pixels use the available neighboring
samples and normalize by the actual sample count.

This demonstrates how an image backend can implement blur, Sobel, morphology, thresholding, or
per-pixel model preprocessing without exposing CUDA thread or synchronization details to the C++
pipeline. It is an API example rather than an optimized blur implementation; a production stencil
can later use a tiled shared-memory pattern when profiling justifies it.

## Build and run

The examples are enabled in the normal repository build:

```bash
make
source .colcon_cache/install/setup.bash
ros2 run accelerator accelerator_vector_pipeline_example
ros2 run accelerator accelerator_point_cloud_centroid_example
ros2 run accelerator accelerator_image_box_blur_example
```

The vector example prints scaled values `2 4 6 8` and sum `20`. The point-cloud example prints
centroid `[1, 2, 2]`. The image example prints:

```text
3 3.5 4
4.5 5 5.5
6 6.5 7
```

Set `ACCELERATOR_BUILD_EXAMPLES=OFF` through CMake to skip compiling the example executables.
