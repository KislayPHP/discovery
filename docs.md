# kislayphp_discovery Documentation

## Overview

`kislayphp_discovery` is the service registry for KislayPHP.

Use it to:
- register service instances
- maintain heartbeat/status
- resolve a healthy instance URL for Core `ServiceClient` or Gateway resolver code

Keep it limited to resolution concerns.

## Namespace

- Primary: `Kislay\Discovery\ServiceRegistry`, `Kislay\Discovery\ClientInterface`
- Legacy aliases: `KislayPHP\Discovery\ServiceRegistry`, `KislayPHP\Discovery\ClientInterface`

## Public API

### `register(string $name, string $url, ?array $metadata = null, ?string $instanceId = null): bool`
Registers one instance.

### `deregister(string $name, ?string $instanceId = null): bool`
Removes one instance or the whole service entry.

### `resolve(string $name, ?string $key = null): ?string`
Returns one healthy URL.

### `resolveAll(string $name): array`
Returns all healthy URLs ordered by effective weight.

### `list(): array`
Returns service => primary URL.

### `listInstances(string $name): array`
Returns full instance metadata.

### `heartbeat(string $name, ?string $instanceId = null): bool`
Marks one/all matching instances `UP` and refreshes heartbeat.

### `setStatus(string $name, string $status, ?string $instanceId = null): bool`
Allowed statuses:
- `UP`
- `DOWN`
- `OUT_OF_SERVICE`
- `UNKNOWN`

### `setHeartbeatTimeout(int $milliseconds): bool`
Controls stale pruning threshold.

## Selection behavior

Supported balancers:
- `weighted_random` (default)
- `random`
- `round_robin`
- `consistent_hash`

Random selection now uses `std::mt19937`.

## Freshness and pruning

Instance is healthy only when:
- status is `UP`
- heartbeat age is within `heartbeat_timeout_ms`

Stale entries are pruned lazily before local read paths:
- `resolve`
- `resolveAll`
- `list`
- `listInstances`
- `getWeight`

## Concurrency

Registry state uses an RW lock instead of a single plain mutex.
- writes: register, deregister, heartbeat, status updates, prune
- reads: selection/list paths after synchronization work

## Capacity protection

`KISLAY_DISCOVERY_MAX_INSTANCES_PER_SERVICE`
- defaults to `1024`
- registration fails when the cap is exceeded
- standalone HTTP `/register` returns `409`

## Redis backend

Enable with:

```bash
export KISLAY_DISCOVERY_STORAGE=redis
export KISLAY_DISCOVERY_REDIS_HOST=127.0.0.1
export KISLAY_DISCOVERY_REDIS_PORT=6379
export KISLAY_DISCOVERY_REDIS_DB=0
export KISLAY_DISCOVERY_REDIS_PREFIX=kislay:discovery
```

Behavior:
- local state remains as the safe fallback mirror
- registry writes are pushed to Redis
- registry reads synchronize from Redis when available
- Redis failure falls back to in-memory state with warning

This keeps dependencies light while still allowing shared registry state.

## Standalone registry server

```php
<?php

$registry = new Kislay\Discovery\ServiceRegistry();
$registry->listen('0.0.0.0', 9010);
$registry->run();
```

Standalone server exposes registry-only endpoints:
- `POST /register`
- `POST /deregister`
- `POST /heartbeat`
- `POST /status`
- `GET /resolve?name=...`
- `GET /resolve-all?name=...`
- `GET /list`
- `GET /instances?name=...`
- `GET /health`

## Core integration

Expected consumer:
- `Kislay\Core\ServiceClient`

Discovery should hand back:
- clean URL
- instance metadata via `listInstances()` when needed

## Example

```php
<?php

$registry = new Kislay\Discovery\ServiceRegistry();
$registry->register('billing', 'http://10.0.0.10:9000', ['zone' => 'az-1', 'weight' => '2'], 'billing-1');
$registry->register('billing', 'http://10.0.0.11:9000', ['zone' => 'az-2', 'weight' => '1'], 'billing-2');

echo $registry->resolve('billing'), PHP_EOL;
```
