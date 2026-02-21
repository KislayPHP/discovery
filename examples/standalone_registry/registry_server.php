<?php

declare(strict_types=1);

if (!class_exists('Kislay\\Core\\App')) {
    fwrite(STDERR, "Kislay\\Core\\App not found. Load kislayphp/core extension.\n");
    exit(1);
}

$registryClass = class_exists('Kislay\\Discovery\\ServiceRegistry')
    ? 'Kislay\\Discovery\\ServiceRegistry'
    : (class_exists('KislayPHP\\Discovery\\ServiceRegistry') ? 'KislayPHP\\Discovery\\ServiceRegistry' : null);

if ($registryClass === null) {
    fwrite(STDERR, "ServiceRegistry class not found. Load kislayphp_discovery extension.\n");
    exit(1);
}

/** @var object $registry */
$registry = new $registryClass();
$app = new Kislay\Core\App();

if (method_exists($app, 'setOption')) {
    $app->setOption('num_threads', 1);
}

$host = getenv('REGISTRY_HOST') ?: '0.0.0.0';
$port = (int) (getenv('REGISTRY_PORT') ?: '9090');
if ($port < 1 || $port > 65535) {
    $port = 9090;
}

$readJson = static function ($req): array {
    if (is_object($req) && method_exists($req, 'getJson')) {
        try {
            $data = $req->getJson();
            if (is_array($data)) {
                return $data;
            }
        } catch (Throwable $e) {
        }
    }

    if (is_object($req) && method_exists($req, 'getBody')) {
        try {
            $raw = $req->getBody();
            if (is_string($raw) && $raw !== '') {
                $decoded = json_decode($raw, true);
                if (is_array($decoded)) {
                    return $decoded;
                }
            }
        } catch (Throwable $e) {
        }
    }

    $raw = file_get_contents('php://input');
    if (is_string($raw) && $raw !== '') {
        $decoded = json_decode($raw, true);
        if (is_array($decoded)) {
            return $decoded;
        }
    }
    return [];
};

$serviceFromQuery = static function ($req): string {
    if (is_object($req)) {
        if (method_exists($req, 'query')) {
            try {
                $value = $req->query('service');
                if (is_string($value) && $value !== '') {
                    return trim($value);
                }
            } catch (Throwable $e) {
            }
        }
        if (method_exists($req, 'getQuery')) {
            try {
                $value = $req->getQuery('service');
                if (is_string($value) && $value !== '') {
                    return trim($value);
                }
            } catch (Throwable $e) {
            }
        }
    }

    $service = $_GET['service'] ?? '';
    return is_string($service) ? trim($service) : '';
};

$app->post('/v1/register', function ($req, $res) use ($registry, $readJson) {
    $data = $readJson($req);
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

$app->post('/v1/deregister', function ($req, $res) use ($registry, $readJson) {
    $data = $readJson($req);
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

$app->post('/v1/heartbeat', function ($req, $res) use ($registry, $readJson) {
    $data = $readJson($req);
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

$app->post('/v1/status', function ($req, $res) use ($registry, $readJson) {
    $data = $readJson($req);
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

$app->get('/v1/resolve', function ($req, $res) use ($registry, $serviceFromQuery) {
    $service = $serviceFromQuery($req);
    if ($service === '') {
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

$app->get('/v1/instances', function ($req, $res) use ($registry, $serviceFromQuery) {
    $service = $serviceFromQuery($req);
    if ($service === '') {
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
    $res->json(['ok' => true, 'service' => 'registry'], 200);
});

$app->listen($host, $port);
printf("Discovery registry server listening on %s:%d\n", $host, $port);
while (true) {
    usleep(250000);
}
