# KislayPHP Discovery

[![PHP Version](https://img.shields.io/badge/PHP-8.2%2B-blue.svg)](https://php.net)
[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)
[![Build Status](https://img.shields.io/github/actions/workflow/status/KislayPHP/discovery/ci.yml?branch=main&label=CI)](https://github.com/KislayPHP/discovery/actions)
[![PIE](https://img.shields.io/badge/install-pie-blueviolet)](https://github.com/php/pie)

> **Service registry and discovery for PHP microservices.** Register, deregister, and resolve services by name — no Consul, no etcd, no external dependencies.

Part of the [KislayPHP ecosystem](https://skelves.com/kislayphp/docs).

---

## ✨ What It Does

`kislayphp/discovery` provides an in-process service registry. PHP services register themselves on startup and can discover each other by logical name. Integrates natively with `kislayphp/gateway` for dynamic service resolution.

```php
<?php
$registry = new Kislay\Discovery\Registry();
$registry->register('user-service', '127.0.0.1', 9001);

$instance = $registry->resolve('user-service'); // returns "127.0.0.1:9001"
```

---

## 📦 Installation

```bash
pie install kislayphp/discovery
```

Enable in `php.ini`:
```ini
extension=kislayphp_discovery.so
```

---

## 🚀 Quick Start

### Service Registration

```php
<?php
$registry = new Kislay\Discovery\Registry();

// Register this service on startup
$registry->register('order-service', '0.0.0.0', 9002);

// Deregister gracefully on shutdown
register_shutdown_function(function() use ($registry) {
    $registry->deregister('order-service', '0.0.0.0', 9002);
});
```

### Service Resolution

```php
<?php
$registry = new Kislay\Discovery\Registry();

// Resolve a service to host:port
$address = $registry->resolve('order-service');  // "127.0.0.1:9002"
$url = "http://{$address}/api/orders";
```

### Gateway Integration

```php
<?php
$gateway = new Kislay\Gateway\Gateway();
$registry = new Kislay\Discovery\Registry();

$gateway->addServiceRoute('*', '/api/*', 'backend');
$gateway->setResolver(function(string $service) use ($registry): string {
    return 'http://' . $registry->resolve($service);
});

$gateway->listen('0.0.0.0', 80);
```

---

## 📖 Public API

```php
namespace Kislay\Discovery;

class Registry {
    public function register(string $name, string $host, int $port): bool;
    public function deregister(string $name, string $host, int $port): bool;
    public function resolve(string $name): string;         // "host:port"
    public function list(): array;                         // all registered services
    public function health(string $name): bool;            // service health check
}
```

Legacy aliases: `KislayPHP\Discovery\Registry`

---

## 🔗 Ecosystem

[core](https://github.com/KislayPHP/core) · [gateway](https://github.com/KislayPHP/gateway) · **discovery** · [metrics](https://github.com/KislayPHP/metrics) · [queue](https://github.com/KislayPHP/queue) · [eventbus](https://github.com/KislayPHP/eventbus)

## 📄 License

[Apache License 2.0](LICENSE) · **[Full Docs](https://skelves.com/kislayphp/docs)**
