# AI diagnostics

`ai_diagnostics_node_exe` provides advisory fault analysis for the ROS 2 stack.
It consumes either `diagnostic_msgs/msg/DiagnosticArray` messages or the
existing rotating per-node logs, submits bounded evidence to an
OpenAI-compatible model endpoint, and publishes bounded
`autonomy_msgs/msg/AiDiagnosticReport` messages. It never executes a recovery
command or changes another node's state.

Each request contains one incident window: the first warning or error, later
observations through `as_of`, and at most five preceding healthy records from
the same source. A subsequent healthy status from that source closes the
incident. The node determines whether a fault is active from observed events;
the model supplies only an unverified prose analysis. Recovered windows are
not published as active warnings. The published source evidence ID comes from
the observed incident, never from model output.

The model endpoint is a separate service. The default configuration targets
one running on the robot through loopback, so no paid API is required. This
package does not install, start, or schedule the model server. The node gates
requests on sustained low system CPU usage; the model server needs its own
CPU/GPU limits.

## Local model server

A CUDA-enabled `llama-server` is one compatible backend. With an existing GGUF
model file and `llama-server` binary, start it separately from the ROS node:

```bash
llama-server -m /absolute/path/to/model.gguf \
  --alias aegis-diagnostics --host 127.0.0.1 --port 8080 \
  --n-gpu-layers 99 --ctx-size 8192 --reasoning off
```

The default `ai_diagnostics.toml` endpoint, model alias, and empty
`api_key_env` match this command. Wait for the server's `/health` endpoint to
report ready before enabling the diagnostics node. If the server and ROS node
run in different containers or network namespaces, set `endpoint` to an
address reachable from the ROS container; loopback works only when they share
the same network namespace.

For instance, an initial candidate can be the official
[Qwen3-4B Q4_K_M GGUF](https://huggingface.co/Qwen/Qwen3-4B-GGUF/blob/main/Qwen3-4B-Q4_K_M.gguf).
The model file is about 2.5 GB, but CUDA buffers and its context also consume
VRAM. Keep the initial context and evidence budgets modest, then profile peak
VRAM, response latency, and the impact on other GPU dependent modules while they
run. Lower GPU offload or the context size if the combined workload does not
fit. The local model and server are supplied by the deployer; the repository
does not download either one.

After starting the server, run the synthetic fault cases from the sourced ROS
container:

```bash
export AI_DIAGNOSTICS_MODEL_ENDPOINT=http://127.0.0.1:8080/v1/chat/completions
export AI_DIAGNOSTICS_MODEL_NAME=aegis-diagnostics
.colcon_cache/build/ai_diagnostics/test_local_model
```

These cases exercise the production C++ client with a camera/perception failure
chain, a prompt-injection attempt, and four time-bounded episodes from one
synthetic LiDAR log. Five healthy registrations precede each LiDAR episode.
Use the printed responses to compare how the model handles ambiguous drift,
a measured timestamp offset, an independent fixed-target residual, and an
explicitly recovered warning. The test checks transport and response bounds;
it does not require exact model wording or a particular diagnosis.
Without the two environment variables, it reports a skip.
Set them to the exact port and model alias used by the running server; the
example above uses port 8080 and `aegis-diagnostics`. A different server
configuration may use different values. When a case fails, the test continues
with the remaining cases and prints the HTTP status, failure reason, and at
most 2048 bytes of the response body. The excerpt can contain submitted
evidence, so use this diagnostic output only with synthetic or otherwise safe
test data.
This is a model-quality smoke test, not proof of diagnostic accuracy. Record
response time and peak GPU memory with the perception and localization nodes
running before treating the model as an on-robot service.

The same client and cases can be checked without a model using the local canned
server. In one shell run
`python3 src/ai_diagnostics/test/mock_chat_server.py --port 18080`; in another,
set `AI_DIAGNOSTICS_MODEL_ENDPOINT` to
`http://127.0.0.1:18080/v1/chat/completions` and run `test_local_model` as
above. This verifies HTTP transport and extraction of plain-text chat content
only; the canned responses do not measure a model's reasoning quality.

## Run with DDS diagnostics

Source ROS and the workspace, then create runtime configuration files:

```bash
source /opt/ros/jazzy/setup.bash
source .colcon_cache/install/setup.bash
mkdir -p runtime_config
cp "$(ros2 pkg prefix autonomy_config)/share/autonomy_config/config/ai_diagnostics.toml.default" \
  runtime_config/ai_diagnostics.toml
cp "$(ros2 pkg prefix autonomy_config)/share/autonomy_config/config/logging.toml.default" \
  runtime_config/logging.toml
```

Set `enabled = true` and `input_mode = "dds"` in
`runtime_config/ai_diagnostics.toml`. The default `endpoint`, `model`, and
`api_key_env` target the local server shown above. For another compatible
server, set its full chat-completions URL and model name. If that server
requires a bearer token, set `api_key_env` to the name of an exported
environment variable. Set `dds_enabled = true` in
`runtime_config/logging.toml` so nodes using the logging adapter publish their
records on `/diagnostics`.

Run the diagnostics node and the autonomy nodes with the same configuration
directory:

```bash
export AEGIS_AUTONOMY_CONFIG_DIR="$PWD/runtime_config"
ros2 run ai_diagnostics ai_diagnostics_node_exe
```

Inspect reports in another sourced shell:

```bash
ros2 topic echo /ai_diagnostics/report
```

The current localization, place-recognition, and perception executables use
the common logging adapter. The node also accepts diagnostics from other
publishers on its configured `input_topic`. A report appears only when there
is evidence, CPU usage permits analysis, the endpoint responds successfully,
and its response passes transport and size checks. An active fault can produce
an advisory report even when the model cannot identify a cause; a recovered
incident does not publish an active report. The model's `analysis` field is
plain text. The complete HTTP response, including its envelope, is limited by
`maximum_response_bytes` (64 KiB by default); empty, blank, or oversized
responses are rejected. The report's incident ID, source, fault, and source
evidence ID are derived from observations, not from the model. The model is
asked to compare like-for-like measurements against the baseline and say when
evidence is inconclusive.

## Run from rotating log files

Set `input_mode = "log_files"` in `ai_diagnostics.toml`. This mode reads the
resolved logging directory from `logging.toml` or `AEGIS_AUTONOMY_LOG_DIR`.
It does not require `dds_enabled = true`. The directory must be writable by
the diagnostics process because the cursor file is stored beside the logs.

The reader follows each file by device, inode, and byte offset; it tolerates
rotation and incomplete final lines. It sorts the records within each batch
by timestamp and skips an oversized line so later records remain reachable. An
incident window is held across endpoint failures. The on-disk cursor advances
after all windows from the batch are handled; a cursor-write failure is logged
and may cause duplicate reports after restart. Uncommitted log evidence is
replayed after a process restart. DDS input is bounded in memory and cannot be
replayed across a restart
unless its publisher or bag replays it.

In log-file mode, an INFO message beginning with `health=ok` is an explicit
healthy observation and can close an incident from the same node. Other INFO
messages are not treated as proof of recovery. DDS `DiagnosticStatus::OK`
serves the same role. Metric names should carry units, such as
`lidar_odom_offset_ms`; DDS key/value measurements are retained up to eight
per status. Log-file mode also extracts up to eight finite numeric `name=value`
measurements from each retained message, keeping the original text and evidence
ID. It marks the event truncated when additional numeric measurements are
omitted. This gives the model structured baseline and incident values without
assigning a cause in the parser.
The current builder groups subsequent faults into one active window until the
first faulting node reports healthy. It does not prove that every node in the
window shares a cause, and missing healthy statuses can combine separate
incidents. More specific correlation rules are needed for those cases.

## Resource and failure contract

- `queue_capacity` bounds pending DDS events. A full or contended queue drops
  the incoming event; every DDS message is also capped to that many statuses.
- `max_records_per_batch` and `maximum_context_bytes` bound each incident
  window. The builder retains at most five healthy observations per node for
  at most 32 nodes, and marks a window truncated when older evidence is lost.
  The serialized HTTP request and response are bounded separately.
- `cpu_threshold_percent`, `idle_samples_required`, `sample_interval_ms`, and
  `analysis_cooldown_ms` control when analysis can run. No model request runs
  on a ROS callback thread.
- Invalid configuration, including overlong fields and unsupported endpoint
  schemes, fails during initialization. Malformed or oversized chat responses
  and transport failures do not publish reports.
- Failed analysis retains the current window for retry at the configured cooldown.
  During a prolonged outage, the bounded DDS queue may drop newer events;
  file mode retains unread records on disk subject to the logging rotation
  policy.
- The node writes report, rejected-event, failed-analysis, and pending-batch
  counts to its log every 30 seconds while enabled. Reports name the most
  severe observed fault and carry that event's evidence ID.
- The request uses a total timeout, libcurl TLS
  verification, and a bounded response buffer. It does not follow redirects.

Model output is advisory. Every published report should be reviewed against
its underlying incident window before an operator acts on a suggested check.
Enabling the
feature sends bounded node names, source locations, and diagnostic text to the
configured endpoint; choose a trusted endpoint with an appropriate data
handling policy.

Reference retrieval over versioned code, hardware manuals, and runbooks is a
later addition. Retrieved references will have separate IDs and cannot serve
as observations that a fault occurred in a live incident.
