# KislayPHP Discovery Extension Documentation

## Overview

The KislayPHP Discovery extension provides service discovery and registration capabilities for microservices architectures. It supports both in-memory service registry and pluggable client interfaces for external service discovery systems like Consul, etcd, or ZooKeeper.

## Architecture

### Service Registry Pattern
- **In-memory registry**: Default local service storage
- **Client interface**: Pluggable backend support
- **Event-driven**: Integration with EventBus for service change notifications
- **Health monitoring**: Optional health check capabilities

### Service States
- **Registered**: Service is active and discoverable
- **Unregistered**: Service has been removed from registry
- **Healthy**: Service passes health checks (when implemented)
- **Unhealthy**: Service fails health checks

## Installation

### Via PIE
```bash
pie install kislayphp/discovery
```

### Manual Build
```bash
cd kislayphp_discovery/
phpize && ./configure --enable-kislayphp_discovery && make && make install
```

### php.ini Configuration
```ini
extension=kislayphp_discovery.so
```

## API Reference

### KislayPHP\\Discovery\\Discovery Class

The main service discovery management class.

#### Constructor
```php
$discovery = new KislayPHP\\Discovery\\Discovery();
```

#### Service Registration
```php
$discovery->register(string $name, string $url): void
$discovery->deregister(string $name): void
```

#### Service Discovery
```php
$discovery->resolve(string $name): ?string
$discovery->resolveAll(string $name): array
$discovery->listServices(): array
```

#### Client Integration
```php
$discovery->setClient(KislayPHP\\Discovery\\ClientInterface $client): bool
$discovery->setBus(KislayPHP\\EventBus\\EventBus $bus): bool
```

#### Health Monitoring
```php
$discovery->healthCheck(string $name): bool
$discovery->setHealthCheckInterval(int $seconds): void
```

### KislayPHP\\Discovery\\ClientInterface

Interface for external service discovery clients.

```php
interface ClientInterface {
    public function register(string $name, string $url): bool;
    public function deregister(string $name): bool;
    public function resolve(string $name): ?string;
    public function resolveAll(string $name): array;
    public function listServices(): array;
}
```

## Usage Examples

### Basic Service Registration
```php
<?php
use KislayPHP\\Discovery\\Discovery;

$discovery = new Discovery();

// Register services
$discovery->register('user-service', 'http://localhost:8081');
$discovery->register('order-service', 'http://localhost:8082');
$discovery->register('payment-service', 'http://localhost:8083');

// Resolve services
$userServiceUrl = $discovery->resolve('user-service');
echo "User service: $userServiceUrl\n"; // http://localhost:8081

// List all services
$services = $discovery->listServices();
print_r($services);
// Array
// (
//     [user-service] => http://localhost:8081
//     [order-service] => http://localhost:8082
//     [payment-service] => http://localhost:8083
// )
```

### Service Discovery in Microservices
```php
<?php
// User Service
$userApp = new KislayPHP\\Core\\App();
$userDiscovery = new Discovery();

// Register this service
$userDiscovery->register('user-service', 'http://localhost:8081');

$userApp->get('/users/:id', function($req, $res) use ($userDiscovery) {
    $userId = $req->input('id');

    // Discover other services
    $orderServiceUrl = $userDiscovery->resolve('order-service');
    $paymentServiceUrl = $userDiscovery->resolve('payment-service');

    // Fetch user orders
    $orders = file_get_contents("$orderServiceUrl/orders?user_id=$userId");
    $payments = file_get_contents("$paymentServiceUrl/payments?user_id=$userId");

    $res->json([
        'user_id' => $userId,
        'orders' => json_decode($orders, true),
        'payments' => json_decode($payments, true)
    ]);
});

$userApp->listen('0.0.0.0', 8081);
```

### Load Balancing with Service Discovery
```php
<?php
class LoadBalancer {
    private $discovery;
    private $currentIndex = [];

    public function __construct(Discovery $discovery) {
        $this->discovery = $discovery;
    }

    public function getServiceUrl(string $serviceName): ?string {
        $services = $this->discovery->resolveAll($serviceName);

        if (empty($services)) {
            return null;
        }

        // Round-robin load balancing
        if (!isset($this->currentIndex[$serviceName])) {
            $this->currentIndex[$serviceName] = 0;
        }

        $url = $services[$this->currentIndex[$serviceName]];
        $this->currentIndex[$serviceName] = ($this->currentIndex[$serviceName] + 1) % count($services);

        return $url;
    }
}

// Usage
$loadBalancer = new LoadBalancer($discovery);

$app->get('/api/users', function($req, $res) use ($loadBalancer) {
    $serviceUrl = $loadBalancer->getServiceUrl('user-service');

    if (!$serviceUrl) {
        $res->notFound('User service unavailable');
        return;
    }

    // Proxy request to user service
    $response = file_get_contents($serviceUrl . '/users');
    $res->json(json_decode($response, true));
});
```

### Event-Driven Service Discovery
```php
<?php
use KislayPHP\\EventBus\\EventBus;

$discovery = new Discovery();
$eventBus = new EventBus();

// Connect discovery to event bus
$discovery->setBus($eventBus);

// Listen for service registration events
$eventBus->on('service.registered', function($data) {
    echo "Service registered: {$data['name']} at {$data['url']}\n";

    // Update load balancer or proxy configuration
    updateProxyConfig($data['name'], $data['url']);
});

$eventBus->on('service.deregistered', function($data) {
    echo "Service deregistered: {$data['name']}\n";

    // Remove from load balancer
    removeFromProxyConfig($data['name']);
});

// Register services (will emit events)
$discovery->register('api-gateway', 'http://localhost:8080');
$discovery->register('user-service', 'http://localhost:8081');
```

### External Service Discovery Client
```php
<?php
use KislayPHP\\Discovery\\Discovery;

// Custom Consul client
class ConsulClient implements KislayPHP\\Discovery\\ClientInterface {
    private $consulUrl;

    public function __construct(string $consulUrl = 'http://localhost:8500') {
        $this->consulUrl = rtrim($consulUrl, '/');
    }

    public function register(string $name, string $url): bool {
        $data = [
            'ID' => $name,
            'Name' => $name,
            'Address' => parse_url($url, PHP_URL_HOST),
            'Port' => parse_url($url, PHP_URL_PORT) ?: 80,
            'Check' => [
                'HTTP' => $url . '/health',
                'Interval' => '10s'
            ]
        ];

        $ch = curl_init($this->consulUrl . '/v1/agent/service/register');
        curl_setopt($ch, CURLOPT_POST, true);
        curl_setopt($ch, CURLOPT_POSTFIELDS, json_encode($data));
        curl_setopt($ch, CURLOPT_HTTPHEADER, ['Content-Type: application/json']);
        curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);

        $response = curl_exec($ch);
        $httpCode = curl_getinfo($ch, CURLINFO_HTTP_CODE);
        curl_close($ch);

        return $httpCode === 200;
    }

    public function deregister(string $name): bool {
        $ch = curl_init($this->consulUrl . "/v1/agent/service/deregister/$name");
        curl_setopt($ch, CURLOPT_PUT, true);
        curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);

        $response = curl_exec($ch);
        $httpCode = curl_getinfo($ch, CURLINFO_HTTP_CODE);
        curl_close($ch);

        return $httpCode === 200;
    }

    public function resolve(string $name): ?string {
        $services = $this->resolveAll($name);
        return $services[0] ?? null;
    }

    public function resolveAll(string $name): array {
        $ch = curl_init($this->consulUrl . "/v1/health/service/$name?passing");
        curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);

        $response = curl_exec($ch);
        $httpCode = curl_getinfo($ch, CURLINFO_HTTP_CODE);
        curl_close($ch);

        if ($httpCode !== 200) {
            return [];
        }

        $services = json_decode($response, true);
        $urls = [];

        foreach ($services as $service) {
            $address = $service['Service']['Address'];
            $port = $service['Service']['Port'];
            $urls[] = "http://$address:$port";
        }

        return $urls;
    }

    public function listServices(): array {
        $ch = curl_init($this->consulUrl . '/v1/agent/services');
        curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);

        $response = curl_exec($ch);
        $httpCode = curl_getinfo($ch, CURLINFO_HTTP_CODE);
        curl_close($ch);

        if ($httpCode !== 200) {
            return [];
        }

        $services = json_decode($response, true);
        $result = [];

        foreach ($services as $service) {
            $name = $service['Service'];
            $address = $service['Address'];
            $port = $service['Port'];
            $result[$name] = "http://$address:$port";
        }

        return $result;
    }
}

// Usage with Consul
$discovery = new Discovery();
$consulClient = new ConsulClient('http://consul-server:8500');
$discovery->setClient($consulClient);

// Services are now registered with Consul
$discovery->register('my-service', 'http://localhost:8080');
$services = $discovery->listServices();
```

### Health Monitoring
```php
<?php
class HealthMonitoredDiscovery extends Discovery {
    private $healthChecks = [];
    private $lastHealthCheck = [];

    public function register(string $name, string $url, callable $healthCheck = null): void {
        parent::register($name, $url);

        if ($healthCheck) {
            $this->healthChecks[$name] = $healthCheck;
        } else {
            // Default HTTP health check
            $this->healthChecks[$name] = function() use ($url) {
                $ch = curl_init($url . '/health');
                curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
                curl_setopt($ch, CURLOPT_TIMEOUT, 5);
                curl_exec($ch);
                $httpCode = curl_getinfo($ch, CURLINFO_HTTP_CODE);
                curl_close($ch);
                return $httpCode === 200;
            };
        }
    }

    public function healthCheck(string $name): bool {
        if (!isset($this->healthChecks[$name])) {
            return true; // No health check defined, assume healthy
        }

        $healthCheck = $this->healthChecks[$name];
        $isHealthy = $healthCheck();

        $this->lastHealthCheck[$name] = [
            'healthy' => $isHealthy,
            'timestamp' => time()
        ];

        return $isHealthy;
    }

    public function getHealthStatus(): array {
        $status = [];
        foreach ($this->listServices() as $name => $url) {
            $status[$name] = [
                'url' => $url,
                'healthy' => $this->healthCheck($name),
                'last_check' => $this->lastHealthCheck[$name]['timestamp'] ?? null
            ];
        }
        return $status;
    }
}

// Usage
$discovery = new HealthMonitoredDiscovery();

// Register with custom health check
$discovery->register('database-service', 'http://localhost:3306', function() {
    try {
        $pdo = new PDO('mysql:host=localhost;dbname=test', 'user', 'pass');
        return true;
    } catch (Exception $e) {
        return false;
    }
});

// Check health status
$app->get('/health', function($req, $res) use ($discovery) {
    $status = $discovery->getHealthStatus();
    $res->json($status);
});
```

## Client Implementations

### Redis-based Service Discovery
```php
<?php
class RedisDiscoveryClient implements KislayPHP\\Discovery\\ClientInterface {
    private $redis;
    private $prefix;

    public function __construct(string $host = 'localhost', int $port = 6379, string $prefix = 'services:') {
        $this->redis = new Redis();
        $this->redis->connect($host, $port);
        $this->prefix = $prefix;
    }

    public function register(string $name, string $url): bool {
        return $this->redis->set($this->prefix . $name, $url);
    }

    public function deregister(string $name): bool {
        return $this->redis->del($this->prefix . $name) > 0;
    }

    public function resolve(string $name): ?string {
        $url = $this->redis->get($this->prefix . $name);
        return $url ?: null;
    }

    public function resolveAll(string $name): array {
        $url = $this->resolve($name);
        return $url ? [$url] : [];
    }

    public function listServices(): array {
        $keys = $this->redis->keys($this->prefix . '*');
        $services = [];

        foreach ($keys as $key) {
            $name = str_replace($this->prefix, '', $key);
            $services[$name] = $this->redis->get($key);
        }

        return $services;
    }
}
```

### Database-backed Service Discovery
```php
<?php
class DatabaseDiscoveryClient implements KislayPHP\\Discovery\\ClientInterface {
    private $pdo;
    private $table;

    public function __construct(PDO $pdo, string $table = 'services') {
        $this->pdo = $pdo;
        $this->table = $table;

        // Create table if it doesn't exist
        $this->pdo->exec("
            CREATE TABLE IF NOT EXISTS $table (
                name VARCHAR(255) PRIMARY KEY,
                url VARCHAR(500) NOT NULL,
                registered_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                last_seen TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
            )
        ");
    }

    public function register(string $name, string $url): bool {
        $stmt = $this->pdo->prepare("
            INSERT INTO {$this->table} (name, url, registered_at, last_seen)
            VALUES (?, ?, NOW(), NOW())
            ON DUPLICATE KEY UPDATE url = VALUES(url), last_seen = NOW()
        ");
        return $stmt->execute([$name, $url]);
    }

    public function deregister(string $name): bool {
        $stmt = $this->pdo->prepare("DELETE FROM {$this->table} WHERE name = ?");
        return $stmt->execute([$name]);
    }

    public function resolve(string $name): ?string {
        $stmt = $this->pdo->prepare("SELECT url FROM {$this->table} WHERE name = ?");
        $stmt->execute([$name]);
        $result = $stmt->fetch(PDO::FETCH_ASSOC);
        return $result ? $result['url'] : null;
    }

    public function resolveAll(string $name): array {
        $url = $this->resolve($name);
        return $url ? [$url] : [];
    }

    public function listServices(): array {
        $stmt = $this->pdo->query("SELECT name, url FROM {$this->table}");
        $services = [];

        while ($row = $stmt->fetch(PDO::FETCH_ASSOC)) {
            $services[$row['name']] = $row['url'];
        }

        return $services;
    }
}
```

### etcd-based Service Discovery
```php
<?php
class EtcdDiscoveryClient implements KislayPHP\\Discovery\\ClientInterface {
    private $etcdUrl;
    private $httpClient;

    public function __construct(string $etcdUrl = 'http://localhost:2379') {
        $this->etcdUrl = rtrim($etcdUrl, '/');
        $this->httpClient = new GuzzleHttp\\Client();
    }

    public function register(string $name, string $url): bool {
        try {
            $this->httpClient->put($this->etcdUrl . '/v3/kv/put', [
                'json' => [
                    'key' => base64_encode("services/$name"),
                    'value' => base64_encode($url)
                ]
            ]);
            return true;
        } catch (Exception $e) {
            return false;
        }
    }

    public function deregister(string $name): bool {
        try {
            $this->httpClient->post($this->etcdUrl . '/v3/kv/deleterange', [
                'json' => [
                    'key' => base64_encode("services/$name"),
                    'range_end' => base64_encode("services/$name\x00")
                ]
            ]);
            return true;
        } catch (Exception $e) {
            return false;
        }
    }

    public function resolve(string $name): ?string {
        $services = $this->resolveAll($name);
        return $services[0] ?? null;
    }

    public function resolveAll(string $name): array {
        try {
            $response = $this->httpClient->post($this->etcdUrl . '/v3/kv/range', [
                'json' => [
                    'key' => base64_encode("services/$name")
                ]
            ]);

            $data = json_decode($response->getBody(), true);
            $urls = [];

            foreach ($data['kvs'] ?? [] as $kv) {
                $urls[] = base64_decode($kv['value']);
            }

            return $urls;
        } catch (Exception $e) {
            return [];
        }
    }

    public function listServices(): array {
        try {
            $response = $this->httpClient->post($this->etcdUrl . '/v3/kv/range', [
                'json' => [
                    'key' => base64_encode('services/'),
                    'range_end' => base64_encode('services0')
                ]
            ]);

            $data = json_decode($response->getBody(), true);
            $services = [];

            foreach ($data['kvs'] ?? [] as $kv) {
                $key = base64_decode($kv['key']);
                $name = str_replace('services/', '', $key);
                $services[$name] = base64_decode($kv['value']);
            }

            return $services;
        } catch (Exception $e) {
            return [];
        }
    }
}
```

## Advanced Usage

### Service Mesh Integration
```php
<?php
class ServiceMeshDiscovery extends Discovery {
    private $sidecarPort;

    public function __construct(int $sidecarPort = 15000) {
        parent::__construct();
        $this->sidecarPort = $sidecarPort;
    }

    public function resolve(string $name): ?string {
        // Use service mesh sidecar for service discovery
        $sidecarUrl = "http://localhost:{$this->sidecarPort}/resolve/$name";

        $ch = curl_init($sidecarUrl);
        curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
        curl_setopt($ch, CURLOPT_TIMEOUT, 2);

        $response = curl_exec($ch);
        $httpCode = curl_getinfo($ch, CURLINFO_HTTP_CODE);
        curl_close($ch);

        if ($httpCode === 200) {
            $data = json_decode($response, true);
            return $data['url'] ?? null;
        }

        // Fallback to local discovery
        return parent::resolve($name);
    }

    public function register(string $name, string $url): void {
        // Register with service mesh control plane
        $this->registerWithControlPlane($name, $url);

        // Also register locally
        parent::register($name, $url);
    }

    private function registerWithControlPlane(string $name, string $url): void {
        $controlPlaneUrl = getenv('SERVICE_MESH_CONTROL_PLANE') ?: 'http://control-plane:8080';

        $data = [
            'service' => $name,
            'url' => $url,
            'metadata' => [
                'version' => '1.0',
                'environment' => getenv('APP_ENV') ?: 'production'
            ]
        ];

        $ch = curl_init($controlPlaneUrl . '/services');
        curl_setopt($ch, CURLOPT_POST, true);
        curl_setopt($ch, CURLOPT_POSTFIELDS, json_encode($data));
        curl_setopt($ch, CURLOPT_HTTPHEADER, ['Content-Type: application/json']);
        curl_exec($ch);
        curl_close($ch);
    }
}
```

### Circuit Breaker Pattern
```php
<?php
class CircuitBreakerDiscovery extends Discovery {
    private $failureCount = [];
    private $lastFailureTime = [];
    private $circuitOpen = [];
    private $failureThreshold = 5;
    private $recoveryTimeout = 60; // seconds

    public function resolve(string $name): ?string {
        if ($this->isCircuitOpen($name)) {
            // Circuit is open, fail fast
            return null;
        }

        $url = parent::resolve($name);

        if ($url === null) {
            $this->recordFailure($name);
        }

        return $url;
    }

    private function isCircuitOpen(string $name): bool {
        if (!isset($this->circuitOpen[$name])) {
            return false;
        }

        // Check if recovery timeout has passed
        if (time() - $this->lastFailureTime[$name] > $this->recoveryTimeout) {
            // Attempt to close circuit
            unset($this->circuitOpen[$name]);
            unset($this->failureCount[$name]);
            return false;
        }

        return true;
    }

    private function recordFailure(string $name): void {
        $this->failureCount[$name] = ($this->failureCount[$name] ?? 0) + 1;
        $this->lastFailureTime[$name] = time();

        if ($this->failureCount[$name] >= $this->failureThreshold) {
            $this->circuitOpen[$name] = true;
        }
    }

    public function getCircuitStatus(): array {
        $status = [];
        foreach ($this->listServices() as $name => $url) {
            $status[$name] = [
                'url' => $url,
                'circuit_open' => $this->circuitOpen[$name] ?? false,
                'failure_count' => $this->failureCount[$name] ?? 0,
                'last_failure' => $this->lastFailureTime[$name] ?? null
            ];
        }
        return $status;
    }
}
```

### Service Discovery with Caching
```php
<?php
class CachedDiscovery extends Discovery {
    private $cache = [];
    private $cacheExpiry = [];
    private $cacheTtl = 30; // seconds

    public function resolve(string $name): ?string {
        $cacheKey = "resolve:$name";

        if (isset($this->cache[$cacheKey]) &&
            (!isset($this->cacheExpiry[$cacheKey]) || $this->cacheExpiry[$cacheKey] > time())) {
            return $this->cache[$cacheKey];
        }

        $url = parent::resolve($name);

        $this->cache[$cacheKey] = $url;
        $this->cacheExpiry[$cacheKey] = time() + $this->cacheTtl;

        return $url;
    }

    public function listServices(): array {
        $cacheKey = 'list_services';

        if (isset($this->cache[$cacheKey]) &&
            (!isset($this->cacheExpiry[$cacheKey]) || $this->cacheExpiry[$cacheKey] > time())) {
            return $this->cache[$cacheKey];
        }

        $services = parent::listServices();

        $this->cache[$cacheKey] = $services;
        $this->cacheExpiry[$cacheKey] = time() + $this->cacheTtl;

        return $services;
    }

    public function invalidateCache(string $serviceName = null): void {
        if ($serviceName === null) {
            $this->cache = [];
            $this->cacheExpiry = [];
        } else {
            // Invalidate specific service cache
            unset($this->cache["resolve:$serviceName"]);
            unset($this->cacheExpiry["resolve:$serviceName"]);
            unset($this->cache['list_services']);
            unset($this->cacheExpiry['list_services']);
        }
    }

    public function setCacheTtl(int $ttl): void {
        $this->cacheTtl = $ttl;
    }
}
```

## Integration Examples

### Docker Compose Service Discovery
```yaml
version: '3.8'
services:
  discovery:
    image: consul:1.15
    ports:
      - "8500:8500"
    command: agent -server -bootstrap -ui -client 0.0.0.0

  user-service:
    build: ./user-service
    environment:
      - CONSUL_URL=http://discovery:8500
      - SERVICE_NAME=user-service
    depends_on:
      - discovery

  order-service:
    build: ./order-service
    environment:
      - CONSUL_URL=http://discovery:8500
      - SERVICE_NAME=order-service
    depends_on:
      - discovery
```

```php
// Service registration in Docker
$discovery = new Discovery();
$consulClient = new ConsulClient(getenv('CONSUL_URL') ?: 'http://localhost:8500');
$discovery->setClient($consulClient);

// Register this service
$serviceName = getenv('SERVICE_NAME');
$servicePort = getenv('PORT') ?: 8080;
$discovery->register($serviceName, "http://$serviceName:$servicePort");
```

### Kubernetes Service Discovery
```php
<?php
class KubernetesDiscovery extends Discovery {
    private $namespace;
    private $kubernetesApi;

    public function __construct(string $namespace = 'default') {
        parent::__construct();
        $this->namespace = $namespace;
        $this->kubernetesApi = $this->createKubernetesClient();
    }

    public function resolve(string $name): ?string {
        try {
            $endpoint = $this->kubernetesApi->get("/api/v1/namespaces/{$this->namespace}/endpoints/$name");
            $subsets = $endpoint['subsets'] ?? [];

            foreach ($subsets as $subset) {
                $addresses = $subset['addresses'] ?? [];
                $ports = $subset['ports'] ?? [];

                if (!empty($addresses) && !empty($ports)) {
                    $address = $addresses[0]['ip'];
                    $port = $ports[0]['port'];
                    return "http://$address:$port";
                }
            }
        } catch (Exception $e) {
            // Service not found or API error
        }

        return null;
    }

    public function listServices(): array {
        try {
            $services = $this->kubernetesApi->get("/api/v1/namespaces/{$this->namespace}/services");
            $result = [];

            foreach ($services['items'] ?? [] as $service) {
                $name = $service['metadata']['name'];
                // In Kubernetes, we typically use service names directly
                $result[$name] = "http://$name.{$this->namespace}.svc.cluster.local";
            }

            return $result;
        } catch (Exception $e) {
            return [];
        }
    }

    private function createKubernetesClient() {
        // Use Kubernetes service account token
        $token = file_get_contents('/var/run/secrets/kubernetes.io/serviceaccount/token');
        $caCert = file_get_contents('/var/run/secrets/kubernetes.io/serviceaccount/ca.crt');

        return new KubernetesApiClient($token, $caCert);
    }
}

// Usage in Kubernetes
$discovery = new KubernetesDiscovery();
$userServiceUrl = $discovery->resolve('user-service');
// Returns: http://user-service.default.svc.cluster.local
```

## Testing

### Unit Testing
```php
<?php
use PHPUnit\\Framework\\TestCase;
use KislayPHP\\Discovery\\Discovery;

class DiscoveryTest extends TestCase {
    private $discovery;

    protected function setUp(): void {
        $this->discovery = new Discovery();
    }

    public function testServiceRegistration() {
        $this->discovery->register('test-service', 'http://localhost:8080');
        $url = $this->discovery->resolve('test-service');

        $this->assertEquals('http://localhost:8080', $url);
    }

    public function testServiceDeregistration() {
        $this->discovery->register('test-service', 'http://localhost:8080');
        $this->discovery->deregister('test-service');

        $url = $this->discovery->resolve('test-service');
        $this->assertNull($url);
    }

    public function testListServices() {
        $this->discovery->register('service1', 'http://localhost:8081');
        $this->discovery->register('service2', 'http://localhost:8082');

        $services = $this->discovery->listServices();

        $this->assertCount(2, $services);
        $this->assertEquals('http://localhost:8081', $services['service1']);
        $this->assertEquals('http://localhost:8082', $services['service2']);
    }

    public function testResolveNonexistentService() {
        $url = $this->discovery->resolve('nonexistent-service');
        $this->assertNull($url);
    }
}
```

### Mock Client for Testing
```php
<?php
class MockDiscoveryClient implements KislayPHP\\Discovery\\ClientInterface {
    private $services = [];

    public function register(string $name, string $url): bool {
        $this->services[$name] = $url;
        return true;
    }

    public function deregister(string $name): bool {
        unset($this->services[$name]);
        return true;
    }

    public function resolve(string $name): ?string {
        return $this->services[$name] ?? null;
    }

    public function resolveAll(string $name): array {
        $url = $this->resolve($name);
        return $url ? [$url] : [];
    }

    public function listServices(): array {
        return $this->services;
    }

    public function setServices(array $services): void {
        $this->services = $services;
    }
}

// Usage in tests
class ServiceTest extends TestCase {
    public function testServiceUsesDiscovery() {
        $mockClient = new MockDiscoveryClient();
        $mockClient->setServices([
            'user-service' => 'http://user-service:8080',
            'order-service' => 'http://order-service:8081'
        ]);

        $discovery = new Discovery();
        $discovery->setClient($mockClient);

        $service = new OrderService($discovery);

        $userServiceUrl = $service->getUserServiceUrl();
        $this->assertEquals('http://user-service:8080', $userServiceUrl);
    }
}
```

### Integration Testing
```php
<?php
class DiscoveryIntegrationTest extends PHPUnit\\Framework\\TestCase {
    private static $serverProcess;

    public static function setUpBeforeClass(): void {
        // Start a test discovery server
        self::$serverProcess = proc_open(
            'php -d extension=kislayphp_discovery.so test_discovery_server.php',
            [],
            $pipes,
            __DIR__ . '/fixtures',
            []
        );
        sleep(2); // Wait for server to start
    }

    public static function tearDownAfterClass(): void {
        if (self::$serverProcess) {
            proc_terminate(self::$serverProcess);
            proc_close(self::$serverProcess);
        }
    }

    public function testEndToEndServiceDiscovery() {
        $discovery = new Discovery();
        $httpClient = new HttpDiscoveryClient('http://localhost:9090');
        $discovery->setClient($httpClient);

        // Register a service
        $result = $discovery->register('test-service', 'http://localhost:8080');
        $this->assertTrue($result);

        // Resolve the service
        $url = $discovery->resolve('test-service');
        $this->assertEquals('http://localhost:8080', $url);

        // List all services
        $services = $discovery->listServices();
        $this->assertArrayHasKey('test-service', $services);

        // Deregister the service
        $result = $discovery->deregister('test-service');
        $this->assertTrue($result);

        // Verify deregistration
        $url = $discovery->resolve('test-service');
        $this->assertNull($url);
    }
}
```

## Troubleshooting

### Common Issues

#### Services Not Being Discovered
**Symptoms:** `resolve()` returns null for registered services

**Solutions:**
1. Check if client is properly set: `$discovery->setClient($client)`
2. Verify client connection to backend service
3. Check service registration logs
4. Ensure service names match exactly (case-sensitive)

#### Connection Timeouts
**Symptoms:** Service discovery operations hang or timeout

**Solutions:**
1. Implement connection timeouts in client
2. Add retry logic with exponential backoff
3. Use circuit breaker pattern
4. Cache discovery results locally

#### Service Registration Failures
**Symptoms:** `register()` returns false

**Solutions:**
1. Check backend service availability
2. Verify authentication credentials
3. Validate service URLs format
4. Check backend service logs for errors

#### Memory Leaks in Long-Running Applications
**Symptoms:** Memory usage grows over time

**Solutions:**
1. Implement proper cleanup in client implementations
2. Use weak references for cached services
3. Implement cache TTL and cleanup routines
4. Monitor memory usage and implement limits

### Debug Logging
```php
<?php
class DebugDiscovery extends Discovery {
    public function register(string $name, string $url): void {
        error_log("Registering service: $name at $url");
        parent::register($name, $url);
        error_log("Service registered successfully: $name");
    }

    public function resolve(string $name): ?string {
        error_log("Resolving service: $name");
        $url = parent::resolve($name);

        if ($url) {
            error_log("Service resolved: $name -> $url");
        } else {
            error_log("Service not found: $name");
        }

        return $url;
    }
}

// Usage
$discovery = new DebugDiscovery();
// All operations will now be logged
```

### Performance Monitoring
```php
<?php
class MonitoredDiscovery extends Discovery {
    private $operationCount = [];
    private $operationTime = [];

    public function register(string $name, string $url): void {
        $start = microtime(true);
        parent::register($name, $url);
        $this->recordOperation('register', microtime(true) - $start);
    }

    public function resolve(string $name): ?string {
        $start = microtime(true);
        $url = parent::resolve($name);
        $this->recordOperation('resolve', microtime(true) - $start);
        return $url;
    }

    private function recordOperation(string $operation, float $duration): void {
        $this->operationCount[$operation] = ($this->operationCount[$operation] ?? 0) + 1;
        $this->operationTime[$operation] = ($this->operationTime[$operation] ?? 0) + $duration;
    }

    public function getMetrics(): array {
        $metrics = [];
        foreach ($this->operationCount as $operation => $count) {
            $metrics[$operation] = [
                'count' => $count,
                'total_time' => $this->operationTime[$operation],
                'avg_time' => $this->operationTime[$operation] / $count
            ];
        }
        return $metrics;
    }
}

// Usage
$discovery = new MonitoredDiscovery();
// ... use discovery ...
$metrics = $discovery->getMetrics();
print_r($metrics);
```

## Best Practices

### Service Naming Conventions
1. **Use kebab-case**: `user-service`, `order-processor`, `payment-gateway`
2. **Include version in name**: `user-service-v1`, `api-gateway-v2`
3. **Use environment prefixes**: `prod-user-service`, `staging-api-gateway`
4. **Be descriptive but concise**: `inventory-management` vs `inv-mgmt`

### Service Registration
1. **Register on startup**: Register services when application starts
2. **Deregister on shutdown**: Clean up registrations when application stops
3. **Include health checks**: Provide health check endpoints for monitoring
4. **Use unique identifiers**: Avoid name collisions with unique service names

### Client Implementations
1. **Implement proper error handling**: Handle network failures gracefully
2. **Use connection pooling**: Reuse connections for better performance
3. **Implement caching**: Cache discovery results to reduce backend load
4. **Support multiple backends**: Allow fallback to different discovery systems

### Monitoring and Observability
1. **Monitor service health**: Track which services are healthy/unhealthy
2. **Log discovery operations**: Record registration and resolution attempts
3. **Track performance metrics**: Monitor latency and success rates
4. **Alert on failures**: Set up alerts for discovery system failures

### Security Considerations
1. **Authenticate with discovery backend**: Use proper credentials
2. **Encrypt sensitive service data**: Don't store secrets in service URLs
3. **Validate service URLs**: Ensure registered URLs are valid and secure
4. **Implement access controls**: Control who can register/discover services

This comprehensive documentation covers all aspects of the KislayPHP Discovery extension, from basic usage to advanced implementations and best practices.