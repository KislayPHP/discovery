<?php

declare(strict_types=1);

require __DIR__ . '/../common/runtime.php';
require __DIR__ . '/../common/HttpDiscoveryClient.php';

try {
    $gatewayClass = kislay_runtime_gateway_class();
} catch (Throwable $e) {
    fwrite(STDERR, $e->getMessage() . "\n");
    exit(1);
}

$registryUrl = getenv('REGISTRY_URL') ?: 'http://127.0.0.1:9090';
$gatewayHost = getenv('GATEWAY_HOST') ?: '0.0.0.0';
$gatewayPort = (int) (getenv('GATEWAY_PORT') ?: '9008');
if ($gatewayPort < 1 || $gatewayPort > 65535) {
    $gatewayPort = 9008;
}
$fallbackTarget = trim((string) (getenv('GATEWAY_FALLBACK_TARGET') ?: ''));
$dynamicResolver = trim((string) (getenv('GATEWAY_DYNAMIC_RESOLVER') ?: '0')) === '1';

$client = new HttpDiscoveryClient($registryUrl);
/** @var object $gateway */
$gateway = new $gatewayClass();
$gateway->setThreads(4);

$routeTable = [
    ['GET', '/health', 'docs-service'],
    ['GET', '/api/health', 'docs-service'],
    ['GET', '/api/site/home', 'docs-service'],
    ['GET', '/api/site/docs', 'docs-service'],
    ['GET', '/api/site/kislayphp', 'docs-service'],
    ['GET', '/api/site/resources', 'docs-service'],
    ['GET', '/api/site/blog', 'blog-service'],
    ['GET', '/api/site/community', 'community-service'],
    ['POST', '/api/auth/login', 'auth-service'],
    ['GET', '/api/auth/me', 'auth-service'],
    ['POST', '/api/auth/logout', 'auth-service'],
];

if ($dynamicResolver) {
    foreach ($routeTable as [$method, $path, $service]) {
        $gateway->addServiceRoute($method, $path, $service);
    }

    $resolvedCache = [];
    $gateway->setResolver(static function (string $service, string $method, string $path) use ($client, &$resolvedCache, $fallbackTarget): string {
        $url = $client->resolve($service);
        if (is_string($url) && $url !== '') {
            $resolvedCache[$service] = $url;
            return $url;
        }

        if (isset($resolvedCache[$service]) && is_string($resolvedCache[$service]) && $resolvedCache[$service] !== '') {
            return $resolvedCache[$service];
        }

        if ($fallbackTarget !== '') {
            return $fallbackTarget;
        }

        return '';
    });
} else {
    $serviceTargets = [];
    $uniqueServices = [];
    foreach ($routeTable as $entry) {
        $uniqueServices[$entry[2]] = true;
    }

    foreach (array_keys($uniqueServices) as $service) {
        $resolved = '';
        for ($i = 0; $i < 40; $i++) {
            $resolved = (string) ($client->resolve($service) ?? '');
            if ($resolved !== '') {
                break;
            }
            usleep(250000);
        }

        if ($resolved === '' && $fallbackTarget !== '') {
            $resolved = $fallbackTarget;
        }

        if ($resolved === '') {
            fwrite(STDERR, "Failed to resolve service target for {$service} from {$registryUrl}\n");
            exit(1);
        }

        $serviceTargets[$service] = $resolved;
    }

    foreach ($routeTable as [$method, $path, $service]) {
        $gateway->addRoute($method, $path, $serviceTargets[$service]);
    }
}

if ($gateway->listen($gatewayHost, $gatewayPort) !== true) {
    fwrite(STDERR, "Failed to start gateway on {$gatewayHost}:{$gatewayPort}\n");
    exit(1);
}

printf("Gateway listening on %s:%d using registry %s\n", $gatewayHost, $gatewayPort, $registryUrl);
while (true) {
    sleep(1);
}
