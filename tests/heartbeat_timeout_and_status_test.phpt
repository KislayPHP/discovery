--TEST--
Kislay Discovery resolves only healthy and fresh instances
--EXTENSIONS--
kislayphp_discovery
--FILE--
<?php
$registry = new Kislay\Discovery\ServiceRegistry();
var_dump($registry->setHeartbeatTimeout(1000));

$registry->register('svc', 'http://127.0.0.1:9001', ['zone' => 'az1'], 'svc-1');
usleep(1200000);
var_dump($registry->resolve('svc'));

var_dump($registry->heartbeat('svc', 'svc-1'));
var_dump($registry->resolve('svc'));

var_dump($registry->setStatus('svc', 'DOWN', 'svc-1'));
var_dump($registry->resolve('svc'));
?>
--EXPECT--
bool(true)
NULL
bool(true)
string(21) "http://127.0.0.1:9001"
bool(true)
NULL
