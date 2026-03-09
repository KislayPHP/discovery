# KislayPHP Discovery Extension - Technical Reference

## Table of Contents

1. [Architecture](#architecture)
2. [Configuration Reference](#configuration-reference)
3. [API Reference](#api-reference)
4. [Patterns and Recipes](#patterns-and-recipes)
5. [Performance Notes](#performance-notes)
6. [Troubleshooting](#troubleshooting)

---

## Architecture

### Overview

The KislayPHP Discovery extension provides a lightweight, in-process service registry with optional gRPC backend support for distributed deployments. The extension manages service instances, health monitoring, and intelligent load balancing across available replicas.

### Registry Model

Services are stored in an in-process registry (`std::unordered_map<string, vector<ServiceInstance>>`), keyed by service name. Each service maintains a list of healthy and unhealthy instances.

#### ServiceInstance Data Structure

```
ServiceInstance {
    string service_name          // E.g., "auth-service"
    string host                  // IPv4/IPv6 address
    int port                     // TCP port number
    int weight                   // 1-100, for weighted load balancing
    bool healthy                 // Current health status
    time_t last_heartbeat        // Epoch timestamp of last heartbeat
    map<string, string> metadata // Custom tags: env, zone, version, etc.
}
```

### Operating Modes

#### In-Process Mode (Default)

- All service state stored in-memory within the PHP process
- O(1) instance lookup by service name
- Thread-safe via `pthread_mutex_t` mutex
- No network latency for discovery queries
- State lost on process restart
- Suitable for single-machine deployments or service-local caching

**Enable:** No configuration required (default)

#### RPC/gRPC Mode

- Delegates discovery to remote gRPC server
- Each discovery query makes RPC call to external registry
- Centralized state across multiple PHP processes/machines
- Introduces ~50-200ms latency per discovery query (configurable)
- Requires `KISLAY_RPC_ENABLED=true` and gRPC endpoint configuration
- Suitable for distributed deployments, Kubernetes clusters, service meshes

**Enable:** Set `KISLAY_RPC_ENABLED=true` and configure `KISLAY_RPC_DISCOVERY_ENDPOINT`

### Thread Safety

All registry operations are protected by `pthread_mutex_t`:

- `register()` — acquires write lock
- `deregister()` — acquires write lock  
- `heartbeat()` — acquires write lock
- `discover()` — acquires read lock (held briefly)
- `discoverAll()` — acquires read lock

Background health-check thread runs independently, periodically marking services unhealthy based on heartbeat timeout.

### Heartbeat Timeout Algorithm

Services must periodically call `heartbeat(name, host, port)` to signal liveness:

1. Service registers with timestamp `last_heartbeat = now()`
2. Background thread checks all services every heartbeat check interval
3. If `now() - last_heartbeat > TTL`, service marked unhealthy
4. If `now() - last_heartbeat > TTL + grace_period`, service auto-deregistered
5. `discover()` and `discoverAll()` only return healthy instances

---

## Configuration Reference

Configure the extension via environment variables. All values are read at module initialization.

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `KISLAY_DISCOVERY_HEARTBEAT_TTL` | int | 30 | Heartbeat timeout in seconds. Services not sending heartbeat within this duration marked unhealthy. |
| `KISLAY_DISCOVERY_DEREGISTER_AFTER` | int | 90 | Auto-deregister timeout in seconds. Services deregistered this many seconds after becoming unhealthy. |
| `KISLAY_DISCOVERY_LOAD_BALANCING` | string | "round_robin" | Default load balancing strategy: `round_robin`, `weighted_random`, or `consistent_hash`. |
| `KISLAY_RPC_ENABLED` | bool | false | Enable gRPC discovery backend. When true, discovery queries delegated to remote server. |
| `KISLAY_RPC_TIMEOUT_MS` | long | 200 | RPC call timeout in milliseconds. |
| `KISLAY_RPC_DISCOVERY_ENDPOINT` | string | "127.0.0.1:9090" | gRPC server address (host:port). |

### Configuration Examples

```bash
# Single-machine, local discovery
export KISLAY_DISCOVERY_HEARTBEAT_TTL=30
export KISLAY_DISCOVERY_LOAD_BALANCING="round_robin"
export KISLAY_RPC_ENABLED=false

# Distributed deployment with central gRPC server
export KISLAY_RPC_ENABLED=true
export KISLAY_RPC_DISCOVERY_ENDPOINT="registry.example.com:9090"
export KISLAY_RPC_TIMEOUT_MS=500
export KISLAY_DISCOVERY_HEARTBEAT_TTL=60
```

---

## API Reference

### __construct()

Initialize the Discovery service. Called automatically on module load.

```php
$discovery = new Discovery();
```

**Returns:** Discovery instance

---

### register()

Register a service instance in the registry.

```php
$discovery->register(
    string $name,
    string $host,
    int $port,
    int $weight = 1,
    array $metadata = []
): bool
```

**Parameters:**
- `$name` (string) — Service name, e.g., "payment-service"
- `$host` (string) — Bind address, e.g., "192.168.1.10" or "::1"
- `$port` (int) — TCP port, e.g., 8080
- `$weight` (int, optional) — Load balancing weight 1-100. Default: 1
- `$metadata` (array, optional) — Custom tags. Default: empty. E.g., `["env" => "prod", "zone" => "us-east-1"]`

**Returns:** `true` on success, `false` on failure

**Example:**

```php
$discovery->register(
    "user-service",
    "10.0.1.5",
    3000,
    weight: 2,
    metadata: ["env" => "production", "version" => "2.1.0", "zone" => "us-west-1"]
);
```

---

### heartbeat()

Send heartbeat to mark service alive. Must be called periodically (recommend every TTL/3 seconds).

```php
$discovery->heartbeat(
    string $name,
    string $host,
    int $port
): bool
```

**Parameters:**
- `$name` (string) — Service name
- `$host` (string) — Service host
- `$port` (int) — Service port

**Returns:** `true` on success, `false` if instance not found or error

**Example:**

```php
// Worker process sends heartbeat every 10 seconds (TTL=30)
$discovery->register("order-service", "10.0.1.8", 4000);

while (true) {
    if (!$discovery->heartbeat("order-service", "10.0.1.8", 4000)) {
        error_log("Heartbeat failed, exiting");
        exit(1);
    }
    sleep(10);
}
```

---

### discover()

Discover a single healthy service instance using configured load balancing strategy.

```php
$discovery->discover(string $name): ?array
```

**Parameters:**
- `$name` (string) — Service name

**Returns:** 
- `array` with keys: `host`, `port`, `weight`, `healthy`, `metadata`
- `null` if no healthy instances found

**Load Balancing Strategies:**
- `round_robin` — Sequential selection with per-service counter
- `weighted_random` — Probability-based selection using weight field
- `consistent_hash` — Deterministic hash-based routing (suitable for stateful services)

**Example:**

```php
$discovery->setLoadBalancingStrategy('weighted_random');
$instance = $discovery->discover("auth-service");

if ($instance) {
    $url = "tcp://{$instance['host']}:{$instance['port']}";
    $client = new AuthClient($url);
    $response = $client->authenticate($token);
} else {
    throw new Exception("auth-service: no healthy instances available");
}
```

---

### discoverAll()

Discover all healthy instances for a service.

```php
$discovery->discoverAll(string $name): array
```

**Parameters:**
- `$name` (string) — Service name

**Returns:** Array of instances (empty array if none found or all unhealthy)

**Example:**

```php
$instances = $discovery->discoverAll("cache-service");
if (empty($instances)) {
    throw new Exception("cache-service unavailable");
}

// Connect to all instances for distributed caching
$pool = [];
foreach ($instances as $inst) {
    $pool[] = new RedisClient("{$inst['host']}:{$inst['port']}");
}
```

---

### deregister()

Remove a service instance from the registry.

```php
$discovery->deregister(
    string $name,
    string $host,
    int $port
): bool
```

**Parameters:**
- `$name` (string) — Service name
- `$host` (string) — Service host
- `$port` (int) — Service port

**Returns:** `true` on success, `false` if not found

**Example:**

```php
// Graceful shutdown
register_shutdown_function(function() {
    global $discovery;
    $discovery->deregister("api-gateway", "10.0.2.1", 8080);
});
```

---

### setOption()

Configure discovery parameters at runtime.

```php
$discovery->setOption(string $key, mixed $value): bool
```

**Parameters:**
- `$key` (string) — Option name
- `$value` (mixed) — Option value

**Returns:** `true` on success, `false` on invalid key/value

**Supported Options:**
- `load_balancing` (string) — `round_robin`, `weighted_random`, `consistent_hash`
- `heartbeat_ttl` (int) — Seconds before marking service unhealthy

---

### setLoadBalancingStrategy()

Set the load balancing strategy for subsequent discovery queries.

```php
$discovery->setLoadBalancingStrategy(string $strategy): bool
```

**Parameters:**
- `$strategy` (string) — One of: `round_robin`, `weighted_random`, `consistent_hash`

**Returns:** `true` on success, `false` on invalid strategy

**Example:**

```php
$discovery->setLoadBalancingStrategy('consistent_hash');
```

---

### getServices()

List all registered service names.

```php
$discovery->getServices(): array
```

**Returns:** Array of service names

**Example:**

```php
$services = $discovery->getServices();
// Returns: ["auth-service", "order-service", "payment-service"]
foreach ($services as $svc) {
    $instances = $discovery->discoverAll($svc);
    echo "$svc: " . count($instances) . " healthy instances\n";
}
```

---

### isHealthy()

Check if a specific instance is healthy.

```php
$discovery->isHealthy(
    string $name,
    string $host,
    int $port
): bool
```

**Parameters:**
- `$name` (string) — Service name
- `$host` (string) — Service host
- `$port` (int) — Service port

**Returns:** `true` if healthy, `false` if unhealthy or not found

---

### markUnhealthy()

Manually mark an instance as unhealthy (e.g., after failed requests).

```php
$discovery->markUnhealthy(
    string $name,
    string $host,
    int $port
): bool
```

**Parameters:**
- `$name` (string) — Service name
- `$host` (string) — Service host
- `$port` (int) — Service port

**Returns:** `true` on success, `false` if not found

**Example:**

```php
function call_with_fallback($service, $request) {
    $instance = $discovery->discover($service);
    
    try {
        $client = new ServiceClient("{$instance['host']}:{$instance['port']}");
        return $client->call($request);
    } catch (ConnectionException $e) {
        // Mark unhealthy and retry with next instance
        $discovery->markUnhealthy($service, $instance['host'], $instance['port']);
        $next = $discovery->discover($service);
        if (!$next) throw $e;
        
        $client = new ServiceClient("{$next['host']}:{$next['port']}");
        return $client->call($request);
    }
}
```

---

### purgeUnhealthy()

Remove all unhealthy instances from registry.

```php
$discovery->purgeUnhealthy(): int
```

**Returns:** Number of instances removed

**Example:**

```php
$removed = $discovery->purgeUnhealthy();
error_log("Purged $removed unhealthy instances from registry");
```

---

## Patterns and Recipes

### Blue-Green Deployment

Deploy new version alongside current version, then switch traffic atomically.

```php
// Phase 1: New version deployed with weight=0 (no traffic)
$discovery->register(
    "api",
    "10.0.1.10",
    8080,
    weight: 0,
    metadata: ["version" => "2.0", "deployment" => "blue"]
);

// Phase 2: Current version at full weight
$discovery->register(
    "api",
    "10.0.1.9",
    8080,
    weight: 100,
    metadata: ["version" => "1.0", "deployment" => "green"]
);

// Phase 3: Traffic switch (atomic)
// Re-register with swapped weights
$discovery->register("api", "10.0.1.10", 8080, weight: 100);
$discovery->register("api", "10.0.1.9", 8080, weight: 0);

// Phase 4: Verify, then deregister old version
sleep(60);  // Monitor metrics
$discovery->deregister("api", "10.0.1.9", 8080);
```

### Canary Rollout (5% Traffic Split)

Route small percentage of traffic to canary version.

```php
// Stable version (95% weight)
$discovery->register(
    "checkout",
    "10.0.1.1",
    3000,
    weight: 95,
    metadata: ["track" => "stable", "version" => "1.2.0"]
);

// Canary version (5% weight)
$discovery->register(
    "checkout",
    "10.0.1.2",
    3000,
    weight: 5,
    metadata: ["track" => "canary", "version" => "1.3.0-rc1"]
);

// Use weighted random strategy
$discovery->setLoadBalancingStrategy('weighted_random');

// Monitor canary metrics
// If healthy after N minutes, increase weight to 50%
$discovery->register("checkout", "10.0.1.2", 3000, weight: 50);
```

### Session Affinity with Consistent Hashing

Route requests from same client to same backend.

```php
$discovery->setLoadBalancingStrategy('consistent_hash');

// Each HTTP request gets client_id from session/auth
$client_id = $_SESSION['user_id'] ?? $_SERVER['REMOTE_ADDR'];

// All discover() calls for same client_id route to same instance
$instance = $discovery->discover("stateful-service");
$conn = new PersistentConnection(
    "{$instance['host']}:{$instance['port']}",
    persistence_key: "svc_" . md5($client_id)
);
```

### Health-Aware Routing with Automatic Failover

Skip unhealthy instances, retry with backoff.

```php
function discover_with_fallback($service, $max_retries = 3) {
    $retries = 0;
    
    while ($retries < $max_retries) {
        $instance = $discovery->discover($service);
        
        if (!$instance) {
            throw new Exception("No available instances for $service");
        }
        
        try {
            return $instance;
        } catch (Exception $e) {
            $discovery->markUnhealthy(
                $service,
                $instance['host'],
                $instance['port']
            );
            $retries++;
            usleep(100000 * $retries);  // Exponential backoff
        }
    }
    
    throw new Exception("Service $service exhausted retries");
}
```

### Zone-Aware Routing

Prefer services in same availability zone to reduce latency.

```php
function discover_in_zone($service, $zone = "us-east-1a") {
    $instances = $discovery->discoverAll($service);
    
    // Prefer instances in same zone
    $same_zone = array_filter($instances, function($inst) use ($zone) {
        return ($inst['metadata']['zone'] ?? null) === $zone;
    });
    
    $candidates = !empty($same_zone) ? $same_zone : $instances;
    return $candidates[array_rand($candidates)];
}
```

---

## Performance Notes

### In-Process Mode

- **Registry Lookup:** O(1) hash table lookup by service name, then O(n) iteration over instances (n typically <20)
- **Heartbeat Overhead:** Minimal; mutex-protected timestamp write, ~0.1ms per call
- **Memory Per Instance:** ~300 bytes (struct overhead + metadata)
- **Typical Latency:** <1ms for `discover()` call
- **Scalability:** Thousands of instances per process; millions across cluster

### RPC/gRPC Mode

- **Network Latency:** ~50-200ms per discovery query (configurable `KISLAY_RPC_TIMEOUT_MS`)
- **CPU Overhead:** Protobuf serialization/deserialization (~1-5% CPU impact)
- **Recommended For:** Distributed deployments, Kubernetes, cross-machine service mesh
- **Trade-off:** Network latency for centralized state consistency and horizontal scaling

### Optimization Tips

1. **Batch discovery queries:** Call `discoverAll()` once and cache results locally for 1-5 seconds
2. **Adjust heartbeat frequency:** More frequent = faster failure detection; less frequent = lower CPU overhead
3. **Use lower TTL for fast failure detection:** TTL=10s for <100ms detection, TTL=60s for lower false positives
4. **Enable consistent_hash for stateful services:** Reduces backend state migration on failures
5. **Periodic purgeUnhealthy():** Call every 5 minutes to prevent memory leaks

### Benchmarks

- Single `discover()` call: 0.3-0.9ms (in-process)
- Single `heartbeat()` call: 0.1-0.5ms
- `discoverAll()` with 50 instances: 1-3ms
- RPC `discover()` call: 50-200ms (network dependent)

---

## Troubleshooting

### Stale Instances (Unhealthy Services Still Returned)

**Symptoms:** `discover()` returns instance that is actually down

**Root Cause:** Heartbeat missed, but background health check hasn't marked unhealthy yet (or TTL too high)

**Solution:**
1. Increase heartbeat frequency: Call `heartbeat()` every 10 seconds instead of 30
2. Lower `KISLAY_DISCOVERY_HEARTBEAT_TTL` to 10-15 seconds
3. Manually mark unhealthy: `$discovery->markUnhealthy($name, $host, $port)` when connection fails

### Heartbeat Failing (heartbeat() Returns False)

**Symptoms:** `heartbeat()` always returns false

**Root Cause:** Service instance not registered, or host:port mismatch

**Solution:**
1. Verify registration: `var_dump($discovery->getServices())`
2. Ensure heartbeat uses exact same host:port as registration
3. Check error logs: `tail -f /var/log/php-errors.log`

### Load Balancing Strategy Not Working

**Symptoms:** All requests go to single instance despite multiple backends

**Root Cause:** Strategy not properly set, or insufficient instances

**Solution:**
1. Verify strategy: Call `$discovery->setLoadBalancingStrategy('weighted_random')`
2. Check instance count: `count($discovery->discoverAll($name))` should be >1
3. For `weighted_random`: Ensure weights differ (not all 1)
4. For `consistent_hash`: Provide stable key (don't use random values)

### RPC Mode Timeout

**Symptoms:** `discover()` slow or failing, errors about RPC timeout

**Root Cause:** gRPC server unreachable, overloaded, or network latency

**Solution:**
1. Verify endpoint reachable: `nc -zv KISLAY_RPC_DISCOVERY_ENDPOINT`
2. Increase timeout: `KISLAY_RPC_TIMEOUT_MS=500` (from default 200)
3. Check gRPC server logs for errors
4. Fallback to in-process mode: Set `KISLAY_RPC_ENABLED=false`

### Memory Leak / Growing Memory Usage

**Symptoms:** PHP process memory grows over time

**Root Cause:** Unhealthy instances not deregistered (auto-deregister timeout too high)

**Solution:**
1. Lower deregister timeout: `KISLAY_DISCOVERY_DEREGISTER_AFTER=60` (from 90)
2. Call `$discovery->purgeUnhealthy()` every minute
3. Monitor service registration: Watch EventBus `discovery.deregister` events

---

## EventBus Integration

The extension emits events to the EventBus for monitoring and logging:

| Event | Payload | When Emitted |
|-------|---------|--------------|
| `discovery.register` | `{name, host, port, weight}` | Service registered |
| `discovery.deregister` | `{name, host, port}` | Service deregistered |
| `discovery.heartbeat` | `{name, host, port, timestamp}` | Heartbeat received |
| `discovery.unhealthy` | `{name, host, port}` | Service marked unhealthy |
| `discovery.healthy` | `{name, host, port}` | Unhealthy service recovered |

**Example Listener:**

```php
$eventBus->on('discovery.unhealthy', function($event) {
    $msg = "Service {$event['name']} unhealthy at {$event['host']}:{$event['port']}";
    error_log($msg);
    // Send alert to monitoring system
    alert_slack("Service DOWN: " . $msg);
});
```

---

## See Also

- [README.md](README.md) — Installation and basic usage
- [Example Implementations](examples/) — Real-world code samples

---

## Support

For issues, questions, or feature requests, visit the GitHub Issues page.

**Version:** 1.0  
**PHP Compatibility:** PHP 7.0+  
**C++ Standard:** C++11  
**License:** See LICENSE file
