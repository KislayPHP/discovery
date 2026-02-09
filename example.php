<?php
// Run from this folder with:
// php -d extension=modules/kislayphp_discovery.so -d extension=../kislay_socket/modules/kislay_socket.so example.php

extension_loaded('kislayphp_discovery') or die('kislayphp_discovery not loaded');
extension_loaded('kislayphp_eventbus') or die('kislayphp_eventbus not loaded');

$registry = new KislayPHP\Discovery\ServiceRegistry();

class ArrayDiscoveryClient implements KislayPHP\Discovery\ClientInterface {
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

$use_client = false;
if ($use_client) {
	$registry->setClient(new ArrayDiscoveryClient());
}
$bus = new KislayPHP\EventBus\Server();
$registry->setBus($bus);
$registry->register('user-service', 'http://127.0.0.1:9001');
$registry->register('order-service', 'http://127.0.0.1:9002');

var_dump($registry->resolve('user-service'));
print_r($registry->list());
