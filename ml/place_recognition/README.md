# Camera and WiFi place recognition

This offline experiment trains one LibTorch place-descriptor model with separate camera and WiFi
branches. The fusion head receives the two descriptors plus explicit availability masks, so the
same model supports camera-only, WiFi-only, and combined inference. Training randomly suppresses
one available modality on some samples so absence is represented during learning.

The WiFi branch consumes robot-side `ap_rssi_synced` and `ap_hw_noise_synced` arrays from a labeled
HDF5 `channels.mat` file. The camera branch consumes RGB frames listed in an optional synchronized
CSV manifest. Images are decoded lazily with OpenCV and resized to 160x120; the dataset never keeps
all decoded frames in memory.

The `labels` values are **pseudo-ground-truth**, not independent survey ground truth. They are an
offline Cartographer estimate produced from LiDAR and odometry. Results must be reported as
retrieval accuracy against that reference trajectory.

## Camera manifest

The manifest maps a WiFi/reference sample row to a synchronized image. Relative image paths are
resolved against the manifest directory. Rows may be omitted when no camera frame is available.

```csv
sample_index,image_path
0,frames/000000.jpg
1,frames/000001.jpg
3,frames/000003.jpg
```

The manifest must already be synchronized. Extracting camera messages from a ROS 1 bag and matching
them to WiFi samples by timestamp is a separate dataset-preparation step; the model loader does not
infer synchronization from filenames.

## Experiment protocol

Samples remain in trajectory order and use contiguous 60% training/reference, 20% validation, and
20% test sections. Random sample splitting is avoided because adjacent sensor samples would leak
nearly identical observations across splits. A query is eligible for Recall@K only when the
reference section contains a pose within 2 m. Positive training samples must be within 1.5 m and at
least 100 samples apart; negatives must be at least 8 m away.

Evaluation reports a raw WiFi fingerprint baseline, learned WiFi-only retrieval, combined retrieval,
and camera-only retrieval when the manifest covers every reference and test sample. Each result
contains Recall@1, Recall@5, Recall@10, eligible query count, and median top-1 reference-pose
distance. LiDAR/NDT must still verify retrieved candidates before a pose correction or loop closure
is accepted.

## Build

The default ROS build does not build this offline experiment. It needs LibTorch, OpenCV, and the
HDF5 C++ development library. CMake does not download dependencies or datasets.

`setup_distrobox.sh` installs CUDA-enabled C++ LibTorch under `/opt/libtorch`, which is the
workspace Makefile default.

```bash
make place_model
```

Run the synthetic camera/WiFi masking test explicitly:

```bash
make place_model_test
```

## Train and evaluate

WiFi-only training remains supported by omitting the manifest:

```bash
build/Debug/place_recognition/place_model \
  train /path/to/channels.mat /path/to/place_model.pt 30
```

Train and evaluate with synchronized camera frames:

```bash
build/Debug/place_recognition/place_model \
  train /path/to/channels.mat /path/to/place_model.pt 30 /path/to/camera_manifest.csv

build/Debug/place_recognition/place_model \
  evaluate /path/to/channels.mat /path/to/place_model.pt /path/to/camera_manifest.csv
```

The model file is a LibTorch parameter archive. A ROS camera-topic adapter and ONNX/TensorRT export
remain deployment boundaries after the offline model beats its raw-fingerprint baseline.
