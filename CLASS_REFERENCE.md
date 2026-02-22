# Discovery Class Reference

Runtime classes exported by `kislayphp/discovery`.

## Namespace

- Primary: `Kislay\\Discovery`
- Legacy alias: `KislayPHP\\Discovery`

## `Kislay\\Discovery\\ClientInterface`

Contract for external discovery backends.

- `register(string $name, string $url)`
  - Register a service endpoint.
- `deregister(string $name)`
  - Remove service registration.
- `resolve(string $name)`
  - Resolve one service endpoint.
- `list()`
  - List known services.

## `Kislay\\Discovery\\ServiceRegistry`

In-process service registry with instance metadata, health, and resolution.

### Constructor

- `__construct()`
  - Create registry with in-memory state.

### Client and Bus Integration

- `setClient(ClientInterface $client)`
  - Use a remote/alternate registry backend.
- `setBus(object $bus)`
  - Attach event bus for registry lifecycle events.

### Service and Instance Lifecycle

- `register(string $name, string $url, ?array $metadata = null, ?string $instanceId = null)`
  - Register a service instance.
- `deregister(string $name, ?string $instanceId = null)`
  - Remove a whole service or one instance.
- `heartbeat(string $name, ?string $instanceId = null)`
  - Mark instance as alive.
- `setStatus(string $name, string $status, ?string $instanceId = null)`
  - Set instance status (`UP`, `DOWN`, etc.).

### Queries and Resolution

- `resolve(string $name)`
  - Resolve a target URL for a service.
- `list()`
  - List service names.
- `listInstances(string $name)`
  - List registered instances for a service.
