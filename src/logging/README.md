# Logging

The package provides two targets:

- `logging_client`: a bounded, non-blocking producer library initialized by
  `base_core::base_node_c`.
- `logging_service`: the sole owner of log files, rotation, console output,
  and optional diagnostic DDS publication.

Start the service before application nodes:

```bash
ros2 run logging logging_service
```

Logs are written below `AEGIS_AUTONOMY_LOG_DIR`, or `./logs` when the variable
is unset. Each node uses a deterministic file name. Files rotate at 10 MiB and
retain two archives in addition to the active file.

Diagnostic DDS publication is disabled by default. Enable bounded batches on
`/diagnostics` explicitly:

```bash
ros2 run logging logging_service --enable-diagnostic-dds
```

Node processes continue when the service is unavailable. Their fixed-capacity
queues overwrite the oldest record on overflow and drop a new record when a
producer encounters queue contention. The client reports its cumulative drop
count with the next successfully transmitted record.

The service uses one bounded 8192-record spdlog queue shared by all connected
nodes. It also overwrites the oldest pending service record on overflow. A
non-blocking socket send failure keeps one pending client record, reconnects at
a bounded interval, and allows the node queue to apply its normal overflow
policy. The sender runs as `SCHED_OTHER`; the service requests `SCHED_OTHER`
with a lower CPU nice value.
