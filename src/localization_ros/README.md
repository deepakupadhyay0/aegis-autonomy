# Localization ROS

`ndt_localization_node` connects the ROS-independent CUDA NDT implementation to a
`sensor_msgs/msg/PointCloud2` stream using `base_core::topic::waiting_subscriber_c`. The subscriber
has no extra message queue: unread samples remain under DDS QoS control, and each received message
retains shared ownership while it is converted and processed.

The default input is the Newer College Multi-Camera LiDAR topic `/os_cloud_node/points`. The first
valid scan creates a frozen reference map for an initial pipeline smoke test. Later scans localize
against that map and successful poses are published as `geometry_msgs/msg/PoseStamped` on
`/localization/ndt_pose`. The output frame defaults to `ndt_map`, whose origin is the first scan.
No TF is published.

Start the node before bag playback so the first scan is retained as the reference:

```bash
source install/setup.bash
ros2 run localization_ros ndt_localization_node --ros-args -p use_sim_time:=true
```

In another terminal:

```bash
source install/setup.bash
ros2 bag play <quad-easy-bag> --clock
```

Inspect a converted bag with `ros2 bag info <bag>`. If its point-cloud topic differs, select it at
runtime:

```bash
ros2 run localization_ros ndt_localization_node --ros-args \
  -p use_sim_time:=true \
  -p points_topic:=/your/point_cloud_topic
```

First-scan mapping only validates message reception, conversion, CUDA execution, and pose output in
the locally overlapping part of the sequence. Full-loop localization requires loading a static map
built from the first loop; it must not be interpreted as long-range localization against one scan.

## Configuration

The node uses the typed defaults in
`autonomy_config/config/localization.toml.default`. To override them, copy that file as
`localization.toml` into a runtime config directory and point the process at it:

```bash
mkdir -p config
cp "$(ros2 pkg prefix autonomy_config)/share/autonomy_config/config/localization.toml.default" \
  config/localization.toml
export AEGIS_AUTONOMY_CONFIG_DIR="$PWD/config"
```

The TOML file generates the configuration types used directly by the map and localizer. It controls
map construction, all NDT solver limits and convergence thresholds, bounded point and voxel
capacities, and the CUDA device. The node loads one `autonomy_config::Localization` object before
allocating host or device buffers. ROS parameters may override only graph wiring
(`points_topic`, `pose_topic`, `map_frame`, and `pose_queue_depth`). ROS owns `use_sim_time`; keep
passing it through `--ros-args` when playing a bag with `--clock`.
