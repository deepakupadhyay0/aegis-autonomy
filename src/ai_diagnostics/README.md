# AI diagnostics

Experimental, advisory-only diagnostics for the autonomy stack. The node can
consume either `diagnostic_msgs/DiagnosticArray` records or the existing
rotating per-node log files. It waits for sustained low system CPU utilization
and sends a bounded chronological observation batch to a configured
OpenAI-compatible endpoint. The endpoint may be an on-robot SLM server or a
local edge service. Validated reports are published as
`autonomy_msgs/AiDiagnosticReport`.

The node never executes recovery actions. It does not start a local model,
modify parameters, restart nodes, or invoke shell commands.

The feature is disabled by default. To enable it:

1. Set `enabled = true`, `endpoint`, and `model` in
   `ai_diagnostics.toml` under `AEGIS_AUTONOMY_CONFIG_DIR`.
2. Select `input_mode = "dds"` or `input_mode = "log_files"` and set
   `minimum_log_level` to the lowest severity the model may inspect.
3. For DDS mode, set `dds_enabled = true` in `logging.toml` so records reach
   `/diagnostics`. Log-file mode uses the resolved logging directory and does
   not duplicate records over DDS.
4. Export the environment variable named by `api_key_env`. Set
   `api_key_env = ""` only for an endpoint that does not require a bearer token.

Requests use JSON mode, a bounded response buffer, TLS verification provided
by libcurl, and the configured total timeout. Reports must still be reviewed by
an operator. Enabling the feature sends bounded error text, node names, and
source locations to the configured endpoint; configure a trusted endpoint that
matches the deployment's data-handling policy.

Log-file mode reads rotation files incrementally and stores an inode/offset
cursor beside them. Parsed records retain evidence identifiers. Model output
is rejected if it cites identifiers that were not present in the supplied
batch, exceeds bounded report fields, or reports confidence outside `[0, 1]`.
The model also returns a bounded diagnostic-memory summary that is supplied to
the next batch, allowing slow trends and unresolved hypotheses to survive log
rotation without replaying the full history. Insufficient-evidence responses
update that memory but do not publish a warning.

The CPU gate controls when this node submits work. A local model server is a
separate process and must have its own CPU/GPU limits if it must not interfere
with autonomy processing.
