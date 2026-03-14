# KislayDiscovery

> Service registration, standalone registry serving, health tracking, and load-balanced resolution for KislayPHP microservices.

[![PHP Version](https://img.shields.io/badge/PHP-8.2+-blue.svg)](https://php.net)
[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)

## Installation

**Via PIE (recommended):**
```bash
pie install kislayphp/discovery:0.0.5
```

Add to `php.ini`:
```ini
extension=kislayphp_discovery.so
```

**Build from source:**
```bash
git clone https://github.com/KislayPHP/discovery.git
cd discovery
phpize
./configure --enable-kislayphp_discovery
make
sudo make install
```

## Requirements

- PHP 8.2+
- `kislayphp/core` for application services
- `kislayphp/gateway` for service-based gateway routing

## Quick Start

### 1. Start the registry server

```php
<?php

$registry = new Kislay\Discovery\ServiceRegistry();
$registry->listen('0.0.0.0', 9010);
$registry->run();
```

### 2. Register a service from another process

```php
<?php

$registry = new Kislay\Discovery\ServiceRegistry('http://127.0.0.1:9010');
$registry->register('user-service', 'http://127.0.0.1:9008', ['zone' => 'az-1'], 'user-1');
```

### 3. Resolve a service from gateway or another service

```php
<?php

$registry = new Kislay\Discovery\ServiceRegistry('http://127.0.0.1:9010');
$url = $registry->resolve('user-service');
var_dump($url);
```

## API Reference

### `ServiceRegistry`

#### `__construct(?string $baseUrl = null)`
Creates a registry instance.
- No argument: local in-memory registry/server mode
- With `http://host:port`: remote client mode using the same API

#### `listen(string $host, int $port): bool`
Sets the bind address for standalone registry mode.

#### `run(): bool`
Starts the standalone registry server and blocks.

#### `register(string $name, string $url, ?array $metadata = null, ?string $instanceId = null): bool`
Registers a service instance.

#### `deregister(string $name, ?string $instanceId = null): bool`
Removes one instance or all instances for a service.

#### `resolve(string $name, ?string $key = null): ?string`
Returns one healthy instance URL.

#### `resolveAll(string $name): array`
Returns all healthy instance URLs.

#### `listInstances(string $name): array`
Returns full instance metadata for a service.

#### `list(): array`
Returns all registered services and their primary URL.

#### `heartbeat(string $name, ?string $instanceId = null): bool`
Refreshes the heartbeat timestamp for one instance or all instances of a service.

#### `setStatus(string $name, string $status, ?string $instanceId = null): bool`
Sets instance status. Supported values:
- `UP`
- `DOWN`
- `OUT_OF_SERVICE`
- `UNKNOWN`

#### `setHeartbeatTimeout(int $milliseconds): bool`
Sets the stale-instance timeout.

#### `setBus(object $bus): bool`
Attaches an event bus for `discovery.*` events.

#### `setClient(Kislay\Discovery\ClientInterface $client): bool`
Advanced extension point for custom transports. Normal remote registry usage does not need this.

## Runtime Behavior

- `resolve()` only returns instances that are `UP` and heartbeat-fresh.
- `resolveAll()` returns healthy instances ordered by effective weight.
- `heartbeat()` marks matching instances as `UP` and refreshes their `lastHeartbeat` timestamp.
- `run()` exposes built-in HTTP endpoints for registration, resolution, listing, heartbeat, and status changes.

## End-to-End Example

### Registry

```php
<?php

$registry = new Kislay\Discovery\ServiceRegistry();
$registry->listen('0.0.0.0', 9010);
$registry->run();
```

### Core service

```php
<?php

use Kislay\Core\App;

$app = new App();
$registry = new Kislay\Discovery\ServiceRegistry('http://127.0.0.1:9010');
$registry->register('sample-service', 'http://127.0.0.1:9008', ['zone' => 'az-1'], 'sample-1');

$app->get('/health', function ($req, $res) {
    $res->send('OK');
});

$app->get('/api/users', function ($req, $res) {
    $res->json([
        ['id' => 1, 'name' => 'John Doe'],
        ['id' => 2, 'name' => 'Jane Smith'],
    ]);
});

$app->listen('0.0.0.0', 9008);
```

### Gateway

```php
<?php

$registry = new Kislay\Discovery\ServiceRegistry('http://127.0.0.1:9010');
$gateway = new Kislay\Gateway\Gateway();

$gateway->addServiceRoute('GET', '/health', 'sample-service');
$gateway->addServiceRoute('GET', '/api/users', 'sample-service');
$gateway->setResolver(function (string $service, string $method, string $path) use ($registry): string {
    $url = $registry->resolve($service);
    if ($url === null) {
        throw new RuntimeException("No healthy instance for {$service}");
    }
    return $url;
});

$gateway->listen('0.0.0.0', 9009);
while (true) {
    sleep(1);
}
```

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `KISLAY_DISCOVERY_HEARTBEAT_TIMEOUT_MS` | `90000` | Max heartbeat age in ms before an instance is stale |
| `KISLAY_RPC_ENABLED` | `0` | Enable RPC transport when built with RPC support |
| `KISLAY_RPC_DISCOVERY_ENDPOINT` | `127.0.0.1:9090` | RPC discovery endpoint |
| `KISLAY_RPC_TIMEOUT_MS` | `200` | RPC call timeout in ms |

## Common Mistakes

- Instantiating `Kislay\Discovery\ClientInterface` directly. It is an interface, not a concrete client.
- Starting a service before the registry server is ready to accept connections.
- Reusing the same `instanceId` for multiple live instances.
- Expecting `resolve()` to return `DOWN` or stale instances.

## Troubleshooting

**`Failed to connect to registry`**
- Start the registry first.
- Check the registry host and port.
- Verify `curl http://127.0.0.1:9010/health` returns `OK`.

**`resolve()` returns `null`**
- Check `list()` and `listInstances()`.
- Check heartbeat timeout and status.
- Check that the registered URL is reachable.

**Gateway returns `502 Service resolver failed`**
- Ensure the service successfully registered.
- Ensure the resolver returns a full URL.
- Ensure the gateway starts after discovery contains a healthy instance.

## License

Licensed under the [Apache License 2.0](LICENSE).
