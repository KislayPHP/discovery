# kislayphp_discovery

PHP extension for service registration and discovery with instance-level health state.

## Version

Current package line: `v0.0.1`.

## Namespace

- Primary classes:
  - `Kislay\Discovery\ServiceRegistry`
  - `Kislay\Discovery\ClientInterface`
- Legacy aliases:
  - `KislayPHP\Discovery\ServiceRegistry`
  - `KislayPHP\Discovery\ClientInterface`

## Installation

Via PIE:

```bash
pie install kislayphp/discovery
```

Enable in `php.ini`:

```ini
extension=kislayphp_discovery.so
```

Build from source:

```bash
phpize
./configure --enable-kislayphp_discovery
make
sudo make install
```

## Quick Start

```php
<?php

$registry = new Kislay\Discovery\ServiceRegistry();
$registry->setHeartbeatTimeout(30000); // 30 seconds

$registry->register('user-service', 'http://127.0.0.1:9001', ['zone' => 'az-1'], 'user-1');
$registry->register('user-service', 'http://127.0.0.1:9002', ['zone' => 'az-2'], 'user-2');

$url = $registry->resolve('user-service');
$instances = $registry->listInstances('user-service');

var_dump($url, $instances);
```

## Standalone Service Registry

You can run `ServiceRegistry` as a separate process and let services register themselves remotely.

Reference implementation:

- `examples/standalone_registry/registry_server.php`
- `examples/standalone_registry/HttpDiscoveryClient.php`
- `examples/standalone_registry/service_example.php`
- `examples/standalone_registry/gateway_example.php`

Flow:

1. Start registry process:

```bash
cd examples/standalone_registry
REGISTRY_HOST=0.0.0.0 REGISTRY_PORT=9090 php registry_server.php
```

2. Start services (self-registration + heartbeat):

```bash
REGISTRY_URL=http://127.0.0.1:9090 SERVICE_NAME=user-service SERVICE_PORT=9101 SERVICE_URL=http://127.0.0.1:9101 SERVICE_ROUTE=/api/users INSTANCE_ID=user-1 php service_example.php
```

```bash
REGISTRY_URL=http://127.0.0.1:9090 SERVICE_NAME=order-service SERVICE_PORT=9102 SERVICE_URL=http://127.0.0.1:9102 SERVICE_ROUTE=/api/orders INSTANCE_ID=order-1 php service_example.php
```

3. Start gateway that consumes registry:

```bash
REGISTRY_URL=http://127.0.0.1:9090 GATEWAY_PORT=9008 php gateway_example.php
```

4. Call gateway routes:

```bash
curl -i http://127.0.0.1:9008/api/users
curl -i http://127.0.0.1:9008/api/orders
```

## Health and Resolution Rules

- `register()` stores instance with status `UP` and heartbeat timestamp set to now.
- `resolve(name)` picks only instances that are:
  - status `UP`
  - fresh (`now - lastHeartbeat <= heartbeatTimeout`)
- If no healthy+fresh instance exists, `resolve()` returns `null`.
- Selection between healthy instances is round-robin.

## Status Values

Allowed values for `setStatus()`:

- `UP`
- `DOWN`
- `OUT_OF_SERVICE`
- `UNKNOWN`

Invalid status throws exception.

## Public API

`Kislay\Discovery\ServiceRegistry` methods:

- `__construct()`
- `setClient(Kislay\Discovery\ClientInterface $client): bool`
- `register(string $name, string $url, ?array $metadata = null, ?string $instanceId = null): bool`
- `deregister(string $name, ?string $instanceId = null): bool`
- `list(): array`
- `resolve(string $name): ?string`
- `listInstances(string $name): array`
- `heartbeat(string $name, ?string $instanceId = null): bool`
- `setStatus(string $name, string $status, ?string $instanceId = null): bool`
- `setHeartbeatTimeout(int $milliseconds): bool`
- `setBus(object $bus): bool`

`Kislay\Discovery\ClientInterface` methods:

- `register(string $name, string $url): bool`
- `deregister(string $name): bool`
- `resolve(string $name): ?string`
- `list(): array`

Optional richer client methods (auto-used by `ServiceRegistry` when present):

- `registerInstance(string $name, string $url, array $metadata = [], ?string $instanceId = null): bool`
- `deregisterInstance(string $name, ?string $instanceId = null): bool`
- `listInstances(string $name): array`
- `heartbeat(string $name, ?string $instanceId = null): bool`
- `setStatus(string $name, string $status, ?string $instanceId = null): bool`

## Heartbeat Timeout Configuration

Default timeout is from env var `KISLAY_DISCOVERY_HEARTBEAT_TIMEOUT_MS` (default `90000`).

Lower bound:

- values below `1000` ms are clamped to `1000` ms with warning.

## Optional RPC Mode

If extension is built with RPC support, remote discovery calls can be enabled with:

- `KISLAY_RPC_ENABLED=1`
- `KISLAY_RPC_DISCOVERY_ENDPOINT` (default `127.0.0.1:9090`)
- `KISLAY_RPC_TIMEOUT_MS` (default `200`)

## Event Bus Hooks

If `setBus($bus)` is configured, registry calls `$bus->emit()` for:

- `discovery.register`
- `discovery.deregister`

Payload shape:

- `name`
- `url`

## Test

```bash
cd kislayphp_discovery
make test
```
