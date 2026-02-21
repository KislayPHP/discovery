<?php
// Run from this folder with:
// php -d extension=modules/kislayphp_discovery.so -d extension=../kislay_socket/modules/kislay_socket.so example.php

function fail(string $message): void {
	echo "FAIL: {$message}\n";
	exit(1);
}

if (!extension_loaded('kislayphp_discovery')) {
	fail('kislayphp_discovery not loaded');
}

$registry = new Kislay\Discovery\ServiceRegistry();

class ArrayDiscoveryClient implements Kislay\Discovery\ClientInterface {
	private array $services = [];

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

	public function list(): array {
		return $this->services;
	}
}

$registry->setClient(new ArrayDiscoveryClient());
if (class_exists('KislayPHP\\EventBus\\Server')) {
	$bus = new KislayPHP\EventBus\Server();
	$registry->setBus($bus);
}

$registry->register('user-service', 'http://127.0.0.1:9001', ['zone' => 'az-1'], 'user-1');
$registry->register('user-service', 'http://127.0.0.1:9003', ['zone' => 'az-2'], 'user-2');
$registry->register('order-service', 'http://127.0.0.1:9002', ['zone' => 'az-1'], 'order-1');

$resolved = $registry->resolve('user-service');
if (!in_array($resolved, ['http://127.0.0.1:9001', 'http://127.0.0.1:9003'], true)) {
	fail('resolve returned unexpected value');
}

$instances = $registry->listInstances('user-service');
if (!is_array($instances) || count($instances) !== 2) {
	fail('listInstances did not return expected instances');
}

$registry->setStatus('user-service', 'DOWN', 'user-1');
$registry->heartbeat('user-service', 'user-2');

$all = $registry->list();
if (!is_array($all) || count($all) < 2) {
	fail('list did not return expected services');
}

echo "OK: discovery example passed\n";
