# Database

`database` provides process-local, asynchronous SQLite persistence without a
ROS, DDS, socket, or service hop in the write path.

Each process lazily creates one `database_handler_c`. The handler owns one
bounded MPSC queue and one low-priority worker. Only that worker accesses the
process's SQLite connection. Separate processes coordinate through SQLite WAL.
Initialize the handler during node setup; the first `get_handler()` call opens
the database and starts the worker and therefore is not a real-time operation.

## Producer contract

- Serialize data before calling `try_store()`.
- Pass the record as an rvalue and treat every mutable alias of its payload as
  immutable after a successful handoff.
- `try_store()` does not wait. It returns `false` for invalid records, queue
  contention, a full queue, or shutdown.
- Inspect `database_statistics_s` to distinguish rejected input, dropped queue
  submissions, committed rows, and failed writes.
- Do not call database APIs from a hard real-time path unless the payload was
  already allocated and serialized outside that path.

## Consumer ownership

The worker reuses a persistent prepared statement, batches records in one
transaction, and binds payloads as BLOB views. The queued shared payload keeps
the bytes alive through `sqlite3_step()`; SQLite does not retain the view after
the step completes.

`shutdown()` rejects new submissions, drains accepted records, joins the
worker, and is idempotent.
