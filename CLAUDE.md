# kislayphp/discovery — notes for AI assistants

Thin service registry: register instances, track health via heartbeats,
resolve healthy URLs. `Kislay\Discovery\ServiceRegistry` (+
`ClientInterface` for a caller-supplied delegated client). Single source
file: `kislayphp_discovery.cpp`. Has both an in-process registry and a
standalone HTTP server mode (`kislayphp_discovery_server_run()`) other
processes can query/register against.

## Accept loop hardening (fixed 2026-08-30 — this is the second half of an earlier fix)

`kislayphp_discovery_server_run()`'s accept loop dispatches each
connection to its own detached `std::thread` — a fix applied in an earlier
session, whose own comment says it "mirrors kislayphp_queue's
Server::run()". That earlier fix only carried over the *thread-per-
connection* half of `queue`'s pattern, not the **I/O deadline + connection
cap** half. Result: a client that connects and sends nothing (or trickles
bytes) parked `kislayphp_http_read_request()`'s blocking `recv()` — and
the thread+fd it owns — forever. Enough such clients (deliberate
slow-loris, or just a flaky network client) exhaust the process. Fixed by
adding `server_io_timeout_ms` (env `KISLAY_DISCOVERY_SERVER_IO_TIMEOUT_MS`,
default 15000ms) via `SO_RCVTIMEO`/`SO_SNDTIMEO`, plus a connection cap —
now genuinely mirroring `queue`'s pattern in full, not just half of it.
**If you copy this accept-loop pattern into a new server mode in this
codebase, copy both halves** — thread-per-connection without a deadline+cap
is an incomplete version of the fix, not a smaller version of it.

## Other things already fixed, worth knowing before you touch related code

- `resolve()` used to take a write lock even for a pure read — now a
  read-lock (`pthread_rwlock_rdlock`) fast path. Don't reintroduce a write
  lock on the read-only resolution path.
- `heartbeat()` used to hold the registry lock across a Redis round-trip
  (blocking every other registry operation for the duration of that I/O)
  and couldn't revive an instance that had just been pruned — both fixed.
  If you touch `heartbeat()`, keep Redis I/O outside the critical section.
- Redis keys now carry a TTL and use a persistent connection pool (was:
  no TTL, reconnect-per-call).

## Testing

Standard phpt, `make test`. 4/4 as of 2026-08-30 (was 3/3 before the
accept-loop fix added a regression test for the slow-loris gap — verified
via `git stash` that the new test genuinely fails against the pre-fix
build, not just a trivially-passing addition).

## Known open issues

None specific to this module as of 2026-08-30.
