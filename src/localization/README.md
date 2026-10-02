# Localization

The `localization` package provides bounded CPU and CUDA implementations of three-dimensional
Normal Distributions Transform (NDT) scan-to-map registration. Its public API contains typed point,
pose, status, and result structures and consumes the generated localization TOML configuration
objects directly; Eigen remains an implementation detail.

## Initial scope

The first benchmark uses the Newer College Multi-Camera `Quad-Easy` sequence:

1. build a reference map from the first loop;
2. localize scans from the second loop;
3. initialize each scan with a controlled pose perturbation;
4. record convergence, translation and rotation error, correspondence count, score, and latency.

Only the Ouster point-cloud topic, calibration, and ground-truth trajectory are required initially.
ROS bag reading, camera processing, IMU fusion, and continuous ROS execution are separate adapters
to add after the registration core is verified.

## API

`ndt_map_c::rebuild()` converts a bounded point span into regularized Gaussian voxel
distributions stored in a sorted contiguous voxel array. Rebuilding allocates and is not a
real-time operation. A successful map is immutable until the next rebuild and may be queried by
concurrent localizers with bounded neighbour searches.

`ndt_localizer_c::localize()` accepts a map, a bounded scan, and an initial scan-to-map pose. It
uses bounded Gauss-Newton iterations and bounded backtracking line search. The result reports the
estimated pose, mean Mahalanobis cost, correspondence count, iteration count, and convergence.
A lower mean cost indicates a better distribution fit; callers must also enforce correspondence and
convergence requirements.

`cuda_ndt_localizer_c` takes a ready map at construction and uploads its sorted voxel indices,
means, and information matrices once. Each `localize()` call uploads one bounded scan and keeps it
on the device across all iterations. CUDA performs point transformation, bounded neighboring-voxel
lookup, correspondence filtering, and reduction of cost, gradient, and the symmetric Hessian. Only
the pose and reduced normal equation cross the host/device boundary per evaluation; the bounded
optimizer and `6x6` solve remain on the CPU.

The CUDA localizer never silently falls back to the CPU. Initialization failures throw
`accelerator_error_c`; runtime failures return `status_e::accelerator_failure`. The CPU
`ndt_localizer_c` remains the numerical reference and explicit fallback selected by the caller.

The map and localizer APIs consume `autonomy_config::Localization::Map` and
`autonomy_config::Localization::Localizer` directly. These types are generated from
`localization.toml.default`; there is no handwritten mirror of the TOML schema.

## ROS 2 adapter

The sibling `localization_ros` package owns ROS parameters, the waiting subscriber, PointCloud2
conversion, and pose publication. Keeping those concerns outside this package preserves a reusable
registration core and makes bag playback one input adapter rather than part of the NDT algorithm.
