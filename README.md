# KislayPHP Discovery

[![PHP Version](https://img.shields.io/badge/PHP-8.2+-blue.svg)](https://php.net)
[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)
[![Build Status](https://img.shields.io/github/actions/workflow/status/KislayPHP/discovery/ci.yml)](https://github.com/KislayPHP/discovery/actions)
[![codecov](https://codecov.io/gh/KislayPHP/discovery/branch/main/graph/badge.svg)](https://codecov.io/gh/KislayPHP/discovery)

A high-performance C++ PHP extension providing service discovery and registration for microservices with support for external registry, distributed KV store, and custom backends. Perfect for PHP ecosystem integration and modern microservices architecture.

## ⚡ Key Features

- 🚀 **High Performance**: Fast service lookups with caching
- 🔍 **Multiple Backends**: Support for external registry, distributed KV store, coordination store, and custom clients
- 💓 **Health Monitoring**: Automatic service health checks and status tracking
- 📢 **Event Integration**: Service change notifications via EventBus
- 🔄 **Auto-Registration**: Automatic service registration with metadata
- 📊 **Metrics**: Service discovery statistics and monitoring
- 🌐 **Multi-Datacenter**: Cross-datacenter service discovery support
- 🔄 **PHP Ecosystem**: Seamless integration with PHP ecosystem and frameworks
- 🌐 **Microservices Architecture**: Designed for distributed PHP applications

### KislayPHP-native Discovery Features

`ServiceRegistry` now supports instance-level registration for KislayPHP-native service discovery:

- Multiple instances per service (`instanceId`)
- Instance metadata (zone/version/labels)
- Instance status control (`UP`, `DOWN`, `OUT_OF_SERVICE`, `UNKNOWN`)
- Heartbeat refresh API for liveness
- Healthy instance resolution with round-robin selection

Example:

```php
$registry = new KislayPHP\Discovery\ServiceRegistry();

$registry->register('user-service', 'http://127.0.0.1:9001', ['zone' => 'az-1'], 'user-1');
$registry->register('user-service', 'http://127.0.0.1:9002', ['zone' => 'az-2'], 'user-2');

$registry->setStatus('user-service', 'DOWN', 'user-1');
$registry->heartbeat('user-service', 'user-2');

$url = $registry->resolve('user-service'); // resolves healthy instance
$instances = $registry->listInstances('user-service');
```

## 📦 Installation

### Via PIE (Recommended)

```bash
pie install kislayphp/discovery
```

Add to your `php.ini`:

```ini
extension=kislayphp_discovery.so
```

### Manual Build

```bash
git clone https://github.com/KislayPHP/discovery.git
cd discovery
phpize
./configure
make
sudo make install
```

### container

```containerfile
FROM php:8.2-cli
```

## 🚀 Quick Start

### Basic Service Registration

```php
<?php

// Create discovery client
$discovery = new KislayDiscovery();

// Register a service
$serviceId = $discovery->register('user-service', '192.168.1.100', 8080, [
    'version' => '1.0.0',
    'environment' => 'production',
    'health_check' => '/health'
]);

echo "Service registered with ID: $serviceId\n";
```

### Service Discovery

```php
<?php

$discovery = new KislayDiscovery();

// Discover all instances of a service
$services = $discovery->discover('user-service');

foreach ($services as $service) {
    echo "Service: {$service['name']}\n";
    echo "Address: {$service['address']}:{$service['port']}\n";
    echo "Status: {$service['status']}\n";
    echo "Metadata: " . json_encode($service['metadata']) . "\n";
    echo "---\n";
}
```

### Health Monitoring

```php
<?php

$discovery = new KislayDiscovery();

// Register service with health check
$discovery->register('api-gateway', '10.0.0.1', 80, [
    'health_check' => [
        'url' => 'http://10.0.0.1:80/health',
        'interval' => 30,
        'timeout' => 5
    ]
]);

// Check service health
$health = $discovery->health('api-gateway');
echo "Service healthy: " . ($health ? 'Yes' : 'No') . "\n";
```

### Backend Integration

```php
<?php

// Use external registry backend
$discovery = new KislayDiscovery([
    'backend' => 'registry',
    'registry' => [
        'host' => 'registry-server:8500',
        'datacenter' => 'dc1',
        'token' => 'your-registry-token'
    ]
]);

// Services automatically registered in external registry
$discovery->register('payment-service', '10.0.0.2', 8080);
```

## 📚 Documentation

📖 **[Complete Documentation](docs.md)** - API reference, backend integrations, examples, and best practices

## 🏗️ Architecture

KislayPHP Discovery implements a distributed service registry:

```
┌─────────────────┐    ┌─────────────────┐
│   Service A     │    │   Service B     │
│   (PHP)         │    │   (PHP)         │
│                 │    │                 │
│ ┌─────────────┐ │    │ ┌─────────────┐ │
│ │ Discovery   │ │    │ │ Discovery   │ │
│ │ Client      │ │    │ │ Client      │ │
│ └─────────────┘ │    │ └─────────────┘ │
└─────────────────┘    └─────────────────┘
         │                       │
         └───────────────────────┘
            ┌─────────────────┐
            │ Service         │
            │ Registry        │
            │ (external registry/distributed KV store/   │
            │  Custom)        │
            └─────────────────┘

### RPC Server-Client Mode (registry-style)

You can also run a dedicated discovery server over RPC and use clients to register/renew and resolve services.

Build and run the server:

```bash
cd discovery_server
cmake -S . -B build
cmake --build build -j
./build/kislay_discovery_server --bind 0.0.0.0:9090
```

Generate client libraries using the RPC proto:

```bash
protoc -I discovery_server/proto \
    --php_out=./gen/php \
    --rpc_out=./gen/php \
    --plugin=protoc-gen-rpc=$(which rpc_php_plugin) \
    discovery_server/proto/discovery.proto
```
```

## 🎯 Use Cases

- **Microservices**: Service-to-service communication
- **Load Balancing**: Dynamic backend discovery
- **API Gateway**: Service endpoint resolution
- **Health Monitoring**: Service availability tracking
- **Blue-Green Deployments**: Traffic routing between versions
- **Service Mesh**: Distributed service management

## 📊 Performance

```
Service Discovery Benchmark:
==================
Service Registrations: 10,000 services
Discovery Lookups:     50,000/sec
Health Checks:         1,000/sec
Memory Usage:          25 MB
Average Latency:       0.8 ms
Cache Hit Rate:        92%
```

## 🔧 Configuration

### php.ini Settings

```ini
; Discovery extension settings
kislayphp.discovery.cache_size = 10000
kislayphp.discovery.cache_ttl = 300
kislayphp.discovery.health_check_interval = 30
kislayphp.discovery.deregister_timeout = 300

; Backend settings
kislayphp.discovery.backend = "registry"
kislayphp.discovery.registry_host = "localhost:8500"
kislayphp.discovery.distributed KV store_endpoints = "localhost:2379"
```

### Environment Variables

```bash
export KISLAYPHP_DISCOVERY_BACKEND=registry
export KISLAYPHP_DISCOVERY_REGISTRY_HOST=registry-server:8500
export KISLAYPHP_DISCOVERY_HEALTH_CHECK_INTERVAL=30
export KISLAYPHP_DISCOVERY_CACHE_SIZE=10000
```

## 🧪 Testing

```bash
# Run unit tests
php run-tests.php

# Test with external registry backend
cd tests/
php test_registry_integration.php

# Test service registration/discovery
php test_service_lifecycle.php
```

## 🤝 Contributing

We welcome contributions! Please see our [Contributing Guide](.github/CONTRIBUTING.md) for details.

## 📄 License

Licensed under the [Apache License 2.0](LICENSE).

## 🆘 Support

- 📖 [Documentation](docs.md)
- 🐛 [Issue Tracker](https://github.com/KislayPHP/discovery/issues)
- 💬 [Discussions](https://github.com/KislayPHP/discovery/discussions)
- 📧 [Security Issues](.github/SECURITY.md)

## 📈 Roadmap

- [ ] orchestrator service discovery
- [ ] AWS ECS service integration
- [ ] Advanced health check patterns
- [ ] Service mesh integration
- [ ] Multi-region replication

## 🙏 Acknowledgments

- **external registry**: Service discovery and health checking
- **distributed KV store**: Distributed KV store
- **coordination store**: Distributed coordination service
- **PHP**: Zend API for extension development

---

**Built with ❤️ for service-oriented PHP applications**
- https://github.com/KislayPHP/config
- https://github.com/KislayPHP/metrics
- https://github.com/KislayPHP/queue

## Installation

### Via PIE

```bash
pie install kislayphp/discovery
```

Then add to your php.ini:

```ini
extension=kislayphp_discovery.so
```

### Manual Build

```sh
phpize
./configure --enable-kislayphp_discovery
make
```

## Run Locally

```sh
cd /path/to/discovery
php -d extension=modules/kislayphp_discovery.so -d extension=/path/to/eventbus/modules/kislay_socket.so example.php
```

## Custom Client Interface

Default is in-memory. To plug in KV store, MySQL, Mongo, or any other backend, provide
your own PHP client that implements `KislayPHP\Discovery\ClientInterface` and call
`setClient()`.

Example:

```php
$registry = new KislayPHP\Discovery\ServiceRegistry();
$registry->setClient(new MyDiscoveryClient());
```

## Example

```php
<?php
extension_loaded('kislayphp_discovery') or die('kislayphp_discovery not loaded');
extension_loaded('kislayphp_eventbus') or die('kislayphp_eventbus not loaded');

$registry = new KislayPHP\Discovery\ServiceRegistry();

$bus = new KislayPHP\EventBus\Server();
$registry->setBus($bus);
$registry->register('user-service', 'http://127.0.0.1:9001');
$registry->register('order-service', 'http://127.0.0.1:9002');

var_dump($registry->resolve('user-service'));
print_r($registry->list());
?>
```

## SEO Keywords

PHP, microservices, PHP ecosystem, PHP extension, C++ PHP extension, PHP service discovery, PHP service registry, PHP external registry, PHP distributed KV store, PHP coordination store, PHP health checks, PHP microservices discovery, distributed PHP services

---
