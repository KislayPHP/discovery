<?php

declare(strict_types=1);

require __DIR__ . '/../common/runtime.php';

try {
    $appClass = kislay_runtime_app_class();
    $registryClass = kislay_runtime_registry_class();
} catch (Throwable $e) {
    fwrite(STDERR, $e->getMessage() . "\n");
    exit(1);
}

/** @var object $registry */
$registry = new $registryClass();
/** @var object $app */
$app = new $appClass();

if (method_exists($app, 'setOption')) {
    $app->setOption('num_threads', 1);
    $app->setOption('log', true);
}

$host = getenv('REGISTRY_HOST') ?: '0.0.0.0';
$port = (int) (getenv('REGISTRY_PORT') ?: '9090');
if ($port < 1 || $port > 65535) {
    $port = 9090;
}

$app->post('/v1/register', function ($req, $res) use ($registry) {
    $data = kislay_runtime_read_json($req);
    $service = isset($data['service']) ? trim((string) $data['service']) : '';
    $url = isset($data['url']) ? trim((string) $data['url']) : '';
    $metadata = isset($data['metadata']) && is_array($data['metadata']) ? $data['metadata'] : [];
    $instanceId = isset($data['instanceId']) ? trim((string) $data['instanceId']) : null;

    if ($service === '' || $url === '') {
        $res->json(['ok' => false, 'error' => 'service and url are required'], 400);
        return;
    }

    try {
        $ok = $registry->register($service, $url, $metadata, $instanceId !== '' ? $instanceId : null);
        $res->json(['ok' => (bool) $ok], $ok ? 200 : 500);
    } catch (Throwable $e) {
        $res->json(['ok' => false, 'error' => $e->getMessage()], 500);
    }
});

$app->post('/v1/deregister', function ($req, $res) use ($registry) {
    $data = kislay_runtime_read_json($req);
    $service = isset($data['service']) ? trim((string) $data['service']) : '';
    $instanceId = isset($data['instanceId']) ? trim((string) $data['instanceId']) : null;

    if ($service === '') {
        $res->json(['ok' => false, 'error' => 'service is required'], 400);
        return;
    }

    try {
        $ok = $registry->deregister($service, $instanceId !== '' ? $instanceId : null);
        $res->json(['ok' => (bool) $ok], $ok ? 200 : 500);
    } catch (Throwable $e) {
        $res->json(['ok' => false, 'error' => $e->getMessage()], 500);
    }
});

$app->post('/v1/heartbeat', function ($req, $res) use ($registry) {
    $data = kislay_runtime_read_json($req);
    $service = isset($data['service']) ? trim((string) $data['service']) : '';
    $instanceId = isset($data['instanceId']) ? trim((string) $data['instanceId']) : null;

    if ($service === '') {
        $res->json(['ok' => false, 'error' => 'service is required'], 400);
        return;
    }

    try {
        $ok = $registry->heartbeat($service, $instanceId !== '' ? $instanceId : null);
        $res->json(['ok' => (bool) $ok], $ok ? 200 : 404);
    } catch (Throwable $e) {
        $res->json(['ok' => false, 'error' => $e->getMessage()], 500);
    }
});

$app->post('/v1/status', function ($req, $res) use ($registry) {
    $data = kislay_runtime_read_json($req);
    $service = isset($data['service']) ? trim((string) $data['service']) : '';
    $status = isset($data['status']) ? trim((string) $data['status']) : '';
    $instanceId = isset($data['instanceId']) ? trim((string) $data['instanceId']) : null;

    if ($service === '' || $status === '') {
        $res->json(['ok' => false, 'error' => 'service and status are required'], 400);
        return;
    }

    try {
        $ok = $registry->setStatus($service, $status, $instanceId !== '' ? $instanceId : null);
        $res->json(['ok' => (bool) $ok], $ok ? 200 : 404);
    } catch (Throwable $e) {
        $res->json(['ok' => false, 'error' => $e->getMessage()], 500);
    }
});

$app->get('/v1/resolve', function ($req, $res) use ($registry) {
    $service = kislay_runtime_query($req, 'service');
    if (!is_string($service) || $service === '') {
        $res->json(['ok' => false, 'error' => 'service query is required'], 400);
        return;
    }

    try {
        $url = $registry->resolve($service);
        if (!is_string($url) || $url === '') {
            $res->json(['ok' => false, 'error' => 'not found'], 404);
            return;
        }
        $res->json(['ok' => true, 'url' => $url], 200);
    } catch (Throwable $e) {
        $res->json(['ok' => false, 'error' => $e->getMessage()], 500);
    }
});

$app->get('/v1/services', function ($req, $res) use ($registry) {
    try {
        $services = $registry->list();
        $res->json(['ok' => true, 'services' => is_array($services) ? $services : []], 200);
    } catch (Throwable $e) {
        $res->json(['ok' => false, 'error' => $e->getMessage()], 500);
    }
});

$app->get('/v1/instances', function ($req, $res) use ($registry) {
    $service = kislay_runtime_query($req, 'service');
    if (!is_string($service) || $service === '') {
        $res->json(['ok' => false, 'error' => 'service query is required'], 400);
        return;
    }

    try {
        $instances = $registry->listInstances($service);
        $res->json(['ok' => true, 'instances' => is_array($instances) ? $instances : []], 200);
    } catch (Throwable $e) {
        $res->json(['ok' => false, 'error' => $e->getMessage()], 500);
    }
});

$app->get('/health', function ($req, $res) {
    $res->json(['ok' => true, 'service' => 'discovery-registry'], 200);
});

printf("Registry listening on %s:%d\n", $host, $port);
$app->listen($host, $port);
