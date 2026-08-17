# AI diagnostics

Experimental, advisory-only diagnostics for the autonomy stack. The node
consumes error-level `diagnostic_msgs/DiagnosticArray` records, waits for
sustained low system CPU utilization, and sends one bounded fault context to a
configured OpenAI-compatible HTTP endpoint. Validated reports are published as
`autonomy_msgs/AiDiagnosticReport`.

The node never executes recovery actions. It does not start a local model,
modify parameters, restart nodes, or invoke shell commands.

The feature is disabled by default. To enable it:

1. Set `enabled = true`, `endpoint`, and `model` in
   `ai_diagnostics.toml` under `AEGIS_AUTONOMY_CONFIG_DIR`.
2. Set `dds_enabled = true` in `logging.toml` so node error records reach
   `/diagnostics`.
3. Export the environment variable named by `api_key_env`. Set
   `api_key_env = ""` only for an endpoint that does not require a bearer token.

Requests use JSON mode, a bounded response buffer, TLS verification provided
by libcurl, and the configured total timeout. Reports must still be reviewed by
an operator. Enabling the feature sends bounded error text, node names, and
source locations to the configured endpoint; configure a trusted endpoint that
matches the deployment's data-handling policy.

The CPU gate controls when this node submits work. A local model server is a
separate process and must have its own CPU/GPU limits if it must not interfere
with autonomy processing.
