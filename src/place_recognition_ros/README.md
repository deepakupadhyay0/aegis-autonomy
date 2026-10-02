# Place recognition ROS

`place_recognition_node` is driven by `sensor_msgs/msg/CompressedImage`. It waits on the image
subscription and polls the optional `autonomy_msgs/msg/WifiObservation` subscription with a zero
timeout only after receiving an image. Consequently, an absent WiFi publisher cannot block
image-only rosbag playback.

Both subscriptions use `base_core::topic::waiting_subscriber_c` with DDS history depth 1. The node
adds no ROS-message queue and does not retain a previous WiFi message. A WiFi observation is fused
only when it is already pending at the image boundary, structurally valid, and within the configured
timestamp tolerance. Otherwise the same model runs with the WiFi mask disabled.

The output is `autonomy_msgs/msg/PlaceDescriptor`. Retrieval and NDT verification consume this
descriptor later; the node does not publish a pose directly.

## Image-only playback

Set the checkpoint path and disable creation of the WiFi subscriber:

```bash
source .colcon_cache/install/setup.bash
ros2 run place_recognition_ros place_recognition_node --ros-args \
  -p use_sim_time:=true \
  -p checkpoint_path:=/path/to/place_model.pt \
  -p wifi_topic:=""
```

Play the bag in another terminal:

```bash
source .colcon_cache/install/setup.bash
ros2 bag play /path/to/bag --clock
```

The default image topic is `/camera/color/image_raw/compressed`. Override `image_topic` when the
bag uses another compressed-image topic. Inspect output with:

```bash
ros2 topic echo /localization/place_descriptor
```

`WifiObservation.features` is a row-major, already normalized tensor. `access_point_indices`
identifies the model slot for each row. The runtime dimensions must match those used when the
checkpoint was trained.
