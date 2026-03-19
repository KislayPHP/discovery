# KislayDiscovery

> Thin service registry for KislayPHP. Register instances, track health, and resolve healthy URLs without embedding business logic.

[![PHP Version](https://img.shields.io/badge/PHP-8.2+-blue.svg)](https://php.net)
[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)

## Installation

**Via PIE (recommended):**
```bash
pie install kislayphp/discovery:0.0.7
```

Add to `php.ini`:
```ini
extension=kislayphp_discovery.so
```

## Role In The Stack

Discovery only does:
- register service instances
- update health/status
- resolve service name to healthy instance URL
- expose thin registry HTTP endpoints when running standalone

Discovery does **not** do:
- business logic
- request execution
- JWT handling
- trace mutation

## Quick Start

### Start the registry

```php
<?php

$registry = new Kislay\Discovery\ServiceRegistry();
$registry->listen('0.0.0.0', 9010);
$registry->run();
```

### Register a service

```php
<?php

$registry = new Kislay\Discovery\ServiceRegistry('http://127.0.0.1:9010');
$registry->register('user-service', 'http://127.0.0.1:9008', ['zone' => 'az-1'], 'user-1');
```

### Resolve a service

```php
<?php

$registry = new Kislay\Discovery\ServiceRegistry('http://127.0.0.1:9010');
$url = $registry->resolve('user-service');
var_dump($url);
```

## Runtime Behavior

- `resolve()` only returns `UP` and heartbeat-fresh instances.
- stale instances are lazily pruned before local read paths return data.
- weighted selection uses `std::mt19937`, not `rand()`.
- registration is capped per service to avoid unbounded growth.
- standalone server mode exposes registry-only HTTP endpoints.

## Optional Redis Storage

Discovery supports:
- `memory` backend by default
- `redis` backend optionally for shared registry state

When Redis is configured:
- writes are mirrored locally and sent to Redis
- reads attempt Redis first
- on Redis failure, Discovery falls back to in-memory state with a warning
- fallback preserves node-local safety, not cluster-wide strong consistency

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `KISLAY_DISCOVERY_HEARTBEAT_TIMEOUT_MS` | `90000` | Max heartbeat age before stale |
| `KISLAY_DISCOVERY_MAX_INSTANCES_PER_SERVICE` | `1024` | Per-service registration cap |
| `KISLAY_DISCOVERY_STORAGE` | `memory` | `memory` or `redis` |
| `KISLAY_DISCOVERY_REDIS_HOST` | `127.0.0.1` | Redis host |
| `KISLAY_DISCOVERY_REDIS_PORT` | `6379` | Redis port |
| `KISLAY_DISCOVERY_REDIS_DB` | `0` | Redis DB index |
| `KISLAY_DISCOVERY_REDIS_TIMEOUT_MS` | `200` | Redis socket timeout |
| `KISLAY_DISCOVERY_REDIS_PASSWORD` | empty | Redis password |
| `KISLAY_DISCOVERY_REDIS_PREFIX` | `kislay:discovery` | Redis key prefix |
| `KISLAY_RPC_ENABLED` | `0` | Enable RPC mode when built with RPC support |
| `KISLAY_RPC_DISCOVERY_ENDPOINT` | `127.0.0.1:9090` | RPC discovery endpoint |
| `KISLAY_RPC_TIMEOUT_MS` | `200` | RPC timeout in ms |

## Notes

- In-memory mode is still the simplest local default.
- Redis mode gives shared registry storage without adding a client library dependency.
- If Redis is unavailable, fallback is safe but degrades to node-local view.

## License
