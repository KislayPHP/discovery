<?php

declare(strict_types=1);

require __DIR__ . '/HttpDiscoveryClient.php';

if (!class_exists('Kislay\\Core\\App')) {
    fwrite(STDERR, "Kislay\\Core\\App not found. Load kislayphp/core extension.\n");
    exit(1);
}

$serviceName = getenv('SERVICE_NAME') ?: 'user-service';
$servicePort = (int) (getenv('SERVICE_PORT') ?: '9101');
$serviceHost = getenv('SERVICE_HOST') ?: '0.0.0.0';
$serviceUrl = getenv('SERVICE_URL') ?: 'http://127.0.0.1:' . $servicePort;
$serviceRoute = getenv('SERVICE_ROUTE') ?: '/api/info';
$instanceId = getenv('INSTANCE_ID') ?: (gethostname() . ':' . $servicePort);
$registryUrl = getenv('REGISTRY_URL') ?: 'http://127.0.0.1:9090';
$heartbeatSec = (int) (getenv('HEARTBEAT_SEC') ?: '10');
if ($heartbeatSec < 1) {
    $heartbeatSec = 10;
}

$client = new HttpDiscoveryClient($registryUrl);
$metadata = [
    'hostname' => gethostname() ?: 'unknown',
    'startedAt' => (string) time(),
];
$registerAttempt = static function () use ($client, $serviceName, $serviceUrl, $metadata, $instanceId): bool {
    return $client->registerInstance($serviceName, $serviceUrl, $metadata, $instanceId);
};

$registered = false;
for ($i = 0; $i < 20; $i++) {
    if ($registerAttempt()) {
        $registered = true;
        break;
    }
    usleep(250000);
}
if (!$registered) {
    fwrite(STDERR, "Failed to register service in registry after retries: {$registryUrl}\n");
}

register_shutdown_function(static function () use ($client, $serviceName, $instanceId): void {
    $client->deregisterInstance($serviceName, $instanceId);
    $client->setStatus($serviceName, 'DOWN', $instanceId);
});

$app = new Kislay\Core\App();
$app->get('/health', function ($req, $res) use ($serviceName, $instanceId) {
    $res->json([
        'ok' => true,
        'service' => $serviceName,
        'instanceId' => $instanceId,
    ], 200);
});

$app->get('/api/info', function ($req, $res) use ($serviceName, $instanceId, $serviceUrl) {
    $res->json([
        'service' => $serviceName,
        'instanceId' => $instanceId,
        'url' => $serviceUrl,
        'time' => date(DATE_ATOM),
    ], 200);
});

if ($serviceRoute !== '/api/info') {
    $app->get($serviceRoute, function ($req, $res) use ($serviceName, $instanceId, $serviceUrl, $serviceRoute) {
        $res->json([
            'service' => $serviceName,
            'instanceId' => $instanceId,
            'url' => $serviceUrl,
            'route' => $serviceRoute,
            'time' => date(DATE_ATOM),
        ], 200);
    });
}

$app->listen($serviceHost, $servicePort);
printf("Service %s (%s) listening on %s:%d and registered to %s\n", $serviceName, $instanceId, $serviceHost, $servicePort, $registryUrl);

$lastBeat = 0;
$lastRegister = 0;
while (true) {
    if (!$registered || (time() - $lastRegister) >= 20) {
        $registered = $registerAttempt();
        $lastRegister = time();
    }
    if (time() - $lastBeat >= $heartbeatSec) {
        if ($registered) {
            $client->heartbeat($serviceName, $instanceId);
        }
        $lastBeat = time();
    }
    usleep(250000);
}
