--TEST--
Kislay Discovery namespace aliases are available
--EXTENSIONS--
kislayphp_discovery
--FILE--
<?php
var_dump(class_exists('Kislay\\Discovery\\ServiceRegistry'));
var_dump(class_exists('KislayPHP\\Discovery\\ServiceRegistry'));
var_dump(interface_exists('Kislay\\Discovery\\ClientInterface'));
var_dump(interface_exists('KislayPHP\\Discovery\\ClientInterface'));

class DiscoveryClientAliasImpl implements KislayPHP\Discovery\ClientInterface {
    public function register(string $name, string $url): bool { return true; }
    public function deregister(string $name): bool { return true; }
    public function resolve(string $name): ?string { return null; }
    public function list(): array { return []; }
}

$registry = new Kislay\Discovery\ServiceRegistry();
var_dump($registry->setClient(new DiscoveryClientAliasImpl()));
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
