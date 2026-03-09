# KislayDiscovery

> Service registration, health tracking, and load-balanced resolution for KislayPHP microservices.

[![PHP Version](https://img.shields.io/badge/PHP-8.2+-blue.svg)](https://php.net)
[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)

## Installation

**Via PIE (recommended):**
```bash
pie install kislayphp/discovery:0.0.4
```

Add to `php.ini`:
```ini
extension=kislayphp_discovery.so
```

**Build from source:**
```bash
git clone https://github.com/KislayPHP/discovery.git
cd discovery && phpize && ./configure --enable-kislayphp_discovery && make && sudo make install
```

## Requirements

- PHP 8.2+
- kislayphp/core for event bus integration (optional)
- kislayphp/gateway for gateway-side resolution (optional)

## Quick Start

```php
<?php
$registry = new Kislay\Discovery\ServiceRegistry();
$registry->setHeartbeatTimeout(30000); // 30 s

// Register two instances of the same service
$registry->register('user-service', 'http://127.0.0.1:9001', ['zone' => 'az-1'], 'user-1');
$registry->register('user-service', 'http://127.0.0.1:9002', ['zone' => 'az-2'], 'user-2');

// Round-robin resolve among healthy instances
$url = $registry->resolve('user-service'); // 'http://127.0.0.1:9001' or '...9002'

// List all instances with metadata
$instances = $registry->listInstances('user-service');
```

## API Reference

### `ServiceRegistry`

#### `__construct()`
Creates a new in-process registry. For distributed registries, supply a `ClientInterface` via `setClient()`.

#### `setClient(Kislay\Discovery\ClientInterface $client): bool`
Delegates all registry operations to a remote client (e.g. HTTP-based client talking to a standalone registry server).

#### `register(string $name, string $url, ?array $metadata = null, ?string $instanceId = null): bool`
Registers a service instance.
- `$name` — logical service name, e.g. `'user-service'`
- `$url` — reachable base URL, e.g. `'http://127.0.0.1:9001'`
- `$metadata` — arbitrary key-value tags (zone, version, weight, …)
- `$instanceId` — stable unique ID; auto-generated if omitted
- Stores instance with status `UP` and heartbeat timestamp = now

#### `deregister(string $name, ?string $instanceId = null): bool`
Removes a service instance. Omit `$instanceId` to remove all instances for `$name`.

#### `resolve(string $name): ?string`
Returns the URL of one healthy instance using round-robin selection.
- Only considers instances that are status `UP` **and** have a fresh heartbeat (`now - lastHeartbeat <= heartbeatTimeout`)
- Returns `null` if no healthy instance exists

#### `resolveAll(string $name): array`
Returns URLs of **all** healthy instances. Alias: `listInstances($name)` returns full instance objects.

#### `listInstances(string $name): array`
Returns full instance data (URL, metadata, status, last heartbeat) for all instances of `$name`.

#### `list(): array`
Returns all registered services and their instances.

#### `heartbeat(string $name, ?string $instanceId = null): bool`
Updates the last-seen timestamp for an instance. Call periodically to keep instances healthy.

#### `setStatus(string $name, string $status, ?string $instanceId = null): bool`
Manually sets instance status.
- `$status` must be one of: `UP`, `DOWN`, `OUT_OF_SERVICE`, `UNKNOWN`
- Throws on invalid status value

#### `setHeartbeatTimeout(int $milliseconds): bool`
Sets the maximum age (in ms) of a heartbeat before an instance is considered stale.
- Values below `1000` ms are clamped to `1000` ms with a warning
- Default: `KISLAY_DISCOVERY_HEARTBEAT_TIMEOUT_MS` env var or `90000` ms

#### `setBus(object $bus): bool`
Attaches an EventBus instance. The registry emits `discovery.register` and `discovery.deregister` events on mutations.

---

### `ClientInterface`

Implement this interface to back `ServiceRegistry` with a remote transport:

| Method | Signature | Description |
|--------|-----------|-------------|
| `register` | `register(string $name, string $url): bool` | Register a service |
| `deregister` | `deregister(string $name): bool` | Remove a service |
| `resolve` | `resolve(string $name): ?string` | Resolve one URL |
| `list` | `list(): array` | List all services |

Extended optional methods (used when present):

| Method | Signature |
|--------|-----------|
| `registerInstance` | `registerInstance(string $name, string $url, array $metadata = [], ?string $instanceId = null): bool` |
| `deregisterInstance` | `deregisterInstance(string $name, ?string $instanceId = null): bool` |
| `listInstances` | `listInstances(string $name): array` |
| `heartbeat` | `heartbeat(string $name, ?string $instanceId = null): bool` |
| `setStatus` | `setStatus(string $name, string $status, ?string $instanceId = null): bool` |

## Configuration

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `KISLAY_DISCOVERY_HEARTBEAT_TIMEOUT_MS` | `90000` | Max heartbeat age in ms before instance is stale |
| `KISLAY_RPC_ENABLED` | `0` | Enable RPC transport for remote registry calls |
| `KISLAY_RPC_DISCOVERY_ENDPOINT` | `127.0.0.1:9090` | Remote registry endpoint |
| `KISLAY_RPC_TIMEOUT_MS` | `200` | RPC call timeout in ms |

## Events

When an EventBus is attached via `setBus()`, the registry emits:

| Event | Payload | Trigger |
|-------|---------|---------|
| `discovery.register` | `{name, url}` | After successful `register()` |
| `discovery.deregister` | `{name, url}` | After successful `deregister()` |

```php
$bus = new Kislay\EventBus\Server();
$registry->setBus($bus);
```

## Examples

### Standalone Registry Server

```bash
# Start the registry
REGISTRY_HOST=0.0.0.0 REGISTRY_PORT=9090 php registry_server.php

# Register a service instance
REGISTRY_URL=http://127.0.0.1:9090 \
SERVICE_NAME=user-service \
SERVICE_URL=http://127.0.0.1:9101 \
INSTANCE_ID=user-1 php service_example.php

# Start gateway that resolves from registry
REGISTRY_URL=http://127.0.0.1:9090 GATEWAY_PORT=8080 php gateway_example.php
```

### Heartbeat Loop

```php
<?php
$registry = new Kislay\Discovery\ServiceRegistry();
$registry->register('my-service', 'http://127.0.0.1:9001', [], 'my-1');

// Send heartbeat every 10 seconds
while (true) {
    $registry->heartbeat('my-service', 'my-1');
    sleep(10);
}
```

### Weight-Based Metadata

```php
$registry->register('api', 'http://10.0.0.1:8080', ['weight' => 3], 'api-1');
$registry->register('api', 'http://10.0.0.2:8080', ['weight' => 1], 'api-2');

// Inspect weights for custom load balancing
$instances = $registry->listInstances('api');
foreach ($instances as $inst) {
    echo $inst['url'] . ' weight=' . ($inst['metadata']['weight'] ?? 1) . "\n";
}
```

## Related Extensions

| Extension | Use Case |
|-----------|----------|
| [kislayphp/gateway](https://github.com/KislayPHP/gateway) | Uses `resolve()` as its upstream resolver |
| [kislayphp/eventbus](https://github.com/KislayPHP/eventbus) | Receives `discovery.*` events via `setBus()` |
| [kislayphp/core](https://github.com/KislayPHP/core) | Hosts the microservices being registered |

## License

Licensed under the [Apache License 2.0](LICENSE).
