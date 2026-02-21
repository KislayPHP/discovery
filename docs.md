# kislayphp_discovery Documentation

## Overview

`kislayphp_discovery` provides service registry primitives for PHP applications, with instance metadata, status management, heartbeat tracking, and healthy-instance resolution.

Namespace:

- Primary: `Kislay\Discovery\ServiceRegistry`, `Kislay\Discovery\ClientInterface`
- Legacy aliases: `KislayPHP\Discovery\ServiceRegistry`, `KislayPHP\Discovery\ClientInterface`

## Class: `Kislay\Discovery\ServiceRegistry`

### Constructor

```php
new Kislay\Discovery\ServiceRegistry()
```

Loads heartbeat timeout from env:

- `KISLAY_DISCOVERY_HEARTBEAT_TIMEOUT_MS` (default `90000`)
- values `< 1000` are clamped to `1000` with warning

### `setClient`

```php
setClient(Kislay\Discovery\ClientInterface $client): bool
```

Sets external client adapter. Throws if object does not implement `ClientInterface`.

### `register`

```php
register(string $name, string $url, ?array $metadata = null, ?string $instanceId = null): bool
```

Registers local instance record:

- `instanceId` defaults to `url` when omitted
- status initialized to `UP`
- heartbeat initialized to current timestamp
- metadata stored as string map

With external client set, method also calls `$client->register($name, $url)` and returns `false` if client returns `false`.

### `deregister`

```php
deregister(string $name, ?string $instanceId = null): bool
```

Without external client:

- with `instanceId`: removes only that instance
- without `instanceId`: removes all instances under service

With external client set:

- delegates to `$client->deregister($name)`
- returns `false` when client returns `false`

### `list`

```php
list(): array
```

Returns service map.

- With external client set: returns `$client->list()`.
- Without client: returns local service => URL map.

### `resolve`

```php
resolve(string $name): ?string
```

Resolution order:

- With external client set: delegates to `$client->resolve($name)`.
- With RPC mode enabled: attempts remote resolution.
- Otherwise local resolution:
  - If service has instances, picks healthy+fresh instance via round-robin.
  - If no instance collection exists, may return fallback URL from service map.

Healthy+fresh condition:

- status is `UP`
- `now_ms - lastHeartbeat <= heartbeat_timeout_ms`

No valid candidate returns `null`.

### `listInstances`

```php
listInstances(string $name): array
```

Returns local/RPC instance details for one service.

Each item format:

- `service`
- `instanceId`
- `url`
- `status`
- `lastHeartbeat` (ms epoch)
- `metadata` (array)

### `heartbeat`

```php
heartbeat(string $name, ?string $instanceId = null): bool
```

Updates heartbeat timestamp and forces status `UP`.

- with `instanceId`: updates one instance
- without `instanceId`: updates all instances in service

Returns `true` if at least one instance was updated.

### `setStatus`

```php
setStatus(string $name, string $status, ?string $instanceId = null): bool
```

Allowed status values:

- `UP`
- `DOWN`
- `OUT_OF_SERVICE`
- `UNKNOWN`

Status is normalized to uppercase before validation. Invalid status throws exception.

Returns `true` if at least one instance was updated.

### `setHeartbeatTimeout`

```php
setHeartbeatTimeout(int $milliseconds): bool
```

Sets freshness threshold used by `resolve()`.

- values `< 1000` are clamped to `1000` with warning.

### `setBus`

```php
setBus(object $bus): bool
```

Stores bus object for event emission.

Events emitted:

- `discovery.register`
- `discovery.deregister`

Payload:

```php
[
  'name' => '<service-name>',
  'url' => '<service-url>'
]
```

## Interface: `Kislay\Discovery\ClientInterface`

Required methods:

```php
interface Kislay\Discovery\ClientInterface {
    public function register(string $name, string $url): bool;
    public function deregister(string $name): bool;
    public function resolve(string $name): ?string;
    public function list(): array;
}
```

## RPC Mode (Optional Build)

If extension is compiled with RPC support, remote calls can be enabled via:

- `KISLAY_RPC_ENABLED=1`
- `KISLAY_RPC_DISCOVERY_ENDPOINT` (default `127.0.0.1:9090`)
- `KISLAY_RPC_TIMEOUT_MS` (default `200`)

RPC is used for supported methods when enabled.

## Example

```php
<?php

$registry = new Kislay\Discovery\ServiceRegistry();
$registry->setHeartbeatTimeout(15000);

$registry->register('billing', 'http://10.0.0.10:9000', ['zone' => 'az-1'], 'billing-1');
$registry->register('billing', 'http://10.0.0.11:9000', ['zone' => 'az-2'], 'billing-2');

echo $registry->resolve('billing') . PHP_EOL;

$registry->setStatus('billing', 'DOWN', 'billing-1');
$registry->heartbeat('billing', 'billing-2');

print_r($registry->listInstances('billing'));
```

## Build and Test

```bash
phpize
./configure --enable-kislayphp_discovery
make
make test
```
