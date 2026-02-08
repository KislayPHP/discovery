# KislayPHP Discovery

KislayPHP Discovery is a lightweight in-memory service registry with optional EventBus integration.

## Key Features

- Register, deregister, resolve, and list services.
- Optional EventBus emit on service register/deregister.
- Simple API for local or dev service discovery.

## Use Cases

- Local service catalogs during development.
- Lightweight service registry for demos and tests.
- Pair with the gateway for simple routing.

## SEO Keywords

PHP service discovery, service registry, microservices, C++ PHP extension, event bus integration

## Repository

- https://github.com/KislayPHP/discovery

## Related Modules

- https://github.com/KislayPHP/core
- https://github.com/KislayPHP/eventbus
- https://github.com/KislayPHP/gateway
- https://github.com/KislayPHP/config
- https://github.com/KislayPHP/metrics
- https://github.com/KislayPHP/queue

## Build

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
