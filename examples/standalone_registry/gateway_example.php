<?php

declare(strict_types=1);

require __DIR__ . '/HttpDiscoveryClient.php';

$gatewayClass = class_exists('Kislay\\Gateway\\Gateway')
    ? 'Kislay\\Gateway\\Gateway'
    : (class_exists('KislayPHP\\Gateway\\Gateway') ? 'KislayPHP\\Gateway\\Gateway' : null);

if ($gatewayClass === null) {
    fwrite(STDERR, "Gateway class not found. Load kislayphp_gateway extension.\n");
    exit(1);
}

$registryUrl = getenv('REGISTRY_URL') ?: 'http://127.0.0.1:9090';
$gatewayHost = getenv('GATEWAY_HOST') ?: '0.0.0.0';
$gatewayPort = (int) (getenv('GATEWAY_PORT') ?: '9008');
if ($gatewayPort < 1 || $gatewayPort > 65535) {
    $gatewayPort = 9008;
}

$client = new HttpDiscoveryClient($registryUrl);
/** @var object $gateway */
$gateway = new $gatewayClass();
$gateway->setThreads(4);

$gateway->addServiceRoute('GET', '/api/users', 'user-service');
$gateway->addServiceRoute('GET', '/api/orders', 'order-service');
$gateway->addServiceRoute('GET', '/api/payments', 'payment-service');

$gateway->setResolver(static function (string $service, string $method, string $path) use ($client): string {
    $url = $client->resolve($service);
    if (!is_string($url) || $url === '') {
        throw new RuntimeException('service not found in registry: ' . $service);
    }
    return $url;
});

$gateway->listen($gatewayHost, $gatewayPort);
printf("Gateway listening on %s:%d using registry %s\n", $gatewayHost, $gatewayPort, $registryUrl);
while (true) {
    sleep(1);
}
