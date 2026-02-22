<?php

declare(strict_types=1);

function kislay_runtime_app_class(): string {
    if (class_exists('Kislay\\Core\\App')) {
        return 'Kislay\\Core\\App';
    }
    if (class_exists('KislayPHP\\Core\\App')) {
        return 'KislayPHP\\Core\\App';
    }
    throw new RuntimeException('Kislay Core App class not found. Load kislayphp_extension.');
}

function kislay_runtime_gateway_class(): string {
    if (class_exists('Kislay\\Gateway\\Gateway')) {
        return 'Kislay\\Gateway\\Gateway';
    }
    if (class_exists('KislayPHP\\Gateway\\Gateway')) {
        return 'KislayPHP\\Gateway\\Gateway';
    }
    throw new RuntimeException('Kislay Gateway class not found. Load kislayphp_gateway.');
}

function kislay_runtime_registry_class(): string {
    if (class_exists('Kislay\\Discovery\\ServiceRegistry')) {
        return 'Kislay\\Discovery\\ServiceRegistry';
    }
    if (class_exists('KislayPHP\\Discovery\\ServiceRegistry')) {
        return 'KislayPHP\\Discovery\\ServiceRegistry';
    }
    throw new RuntimeException('ServiceRegistry class not found. Load kislayphp_discovery.');
}

function kislay_runtime_read_json($req): array {
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
}

function kislay_runtime_query($req, string $key): ?string {
    if (is_object($req)) {
        if (method_exists($req, 'query')) {
            try {
                $value = $req->query($key);
                if (is_string($value)) {
                    return trim($value);
                }
            } catch (Throwable $e) {
            }
        }
        if (method_exists($req, 'getQuery')) {
            try {
                $value = $req->getQuery($key);
                if (is_string($value)) {
                    return trim($value);
                }
            } catch (Throwable $e) {
            }
        }
    }

    if (isset($_GET[$key]) && is_string($_GET[$key])) {
        return trim($_GET[$key]);
    }

    return null;
}

function kislay_runtime_header($req, string $key): ?string {
    $lower = strtolower($key);
    if (is_object($req)) {
        if (method_exists($req, 'header')) {
            try {
                $value = $req->header($lower);
                if (is_string($value)) {
                    return trim($value);
                }
            } catch (Throwable $e) {
            }
        }
        if (method_exists($req, 'getHeader')) {
            try {
                $value = $req->getHeader($lower);
                if (is_string($value)) {
                    return trim($value);
                }
            } catch (Throwable $e) {
            }
        }
    }

    $serverKey = 'HTTP_' . strtoupper(str_replace('-', '_', $key));
    if (isset($_SERVER[$serverKey]) && is_string($_SERVER[$serverKey])) {
        return trim($_SERVER[$serverKey]);
    }

    return null;
}

function kislay_runtime_dataset_defaults(): array {
    return [
        'home' => [],
        'blog' => ['categories' => ['All'], 'posts' => []],
        'community' => [
            'stats' => [],
            'topContributors' => [],
            'upcomingEvents' => [],
            'forumTopics' => [],
        ],
        'kislayPhp' => [
            'hero' => [],
            'quickInstall' => [],
            'codeExample' => '',
            'featureTabs' => [],
            'repos' => [],
        ],
        'docs' => [
            'sections' => [],
        ],
        'resources' => [
            'tutorials' => [],
            'examples' => [],
            'videos' => [],
            'tools' => [],
        ],
    ];
}

function kislay_runtime_load_dataset(string $contentFile): array {
    $dataset = kislay_runtime_dataset_defaults();
    if (!is_file($contentFile)) {
        return $dataset;
    }

    $raw = file_get_contents($contentFile);
    if (!is_string($raw) || $raw === '') {
        return $dataset;
    }

    $decoded = json_decode($raw, true);
    if (!is_array($decoded)) {
        return $dataset;
    }

    return array_replace_recursive($dataset, $decoded);
}

function kislay_runtime_register_with_retry(
    HttpDiscoveryClient $client,
    string $serviceName,
    string $serviceUrl,
    array $metadata,
    string $instanceId,
    int $attempts = 20,
    int $delayMs = 250
): bool {
    for ($i = 0; $i < $attempts; $i++) {
        if ($client->registerInstance($serviceName, $serviceUrl, $metadata, $instanceId)) {
            return true;
        }
        usleep(max(1, $delayMs) * 1000);
    }
    return false;
}

function kislay_runtime_attach_shutdown(HttpDiscoveryClient $client, string $serviceName, string $instanceId): void {
    register_shutdown_function(static function () use ($client, $serviceName, $instanceId): void {
        $client->setStatus($serviceName, 'DOWN', $instanceId);
        $client->deregisterInstance($serviceName, $instanceId);
    });
}

function kislay_runtime_start_service(
    object $app,
    HttpDiscoveryClient $client,
    string $serviceName,
    string $serviceHost,
    int $servicePort,
    string $serviceUrl,
    string $instanceId,
    array $metadata,
    int $heartbeatSec
): void {
    $heartbeatSec = $heartbeatSec < 1 ? 10 : $heartbeatSec;

    $registered = kislay_runtime_register_with_retry(
        $client,
        $serviceName,
        $serviceUrl,
        $metadata,
        $instanceId,
        20,
        250
    );

    if (!$registered) {
        fwrite(STDERR, "warning: initial registration failed for {$serviceName} ({$instanceId})\n");
    }

    kislay_runtime_attach_shutdown($client, $serviceName, $instanceId);

    if (method_exists($app, 'listenAsync')) {
        if ($app->listenAsync($serviceHost, $servicePort) !== true) {
            throw new RuntimeException("listenAsync failed for {$serviceName} on {$serviceHost}:{$servicePort}");
        }
    } else {
        throw new RuntimeException('listenAsync not available on app instance');
    }

    printf(
        "%s listening on %s:%d (%s) instance=%s\n",
        $serviceName,
        $serviceHost,
        $servicePort,
        $serviceUrl,
        $instanceId
    );

    $lastBeat = 0;
    $lastRegister = 0;

    while (true) {
        $now = time();

        if (!$registered || ($now - $lastRegister) >= 20) {
            $registered = $client->registerInstance($serviceName, $serviceUrl, $metadata, $instanceId);
            $lastRegister = $now;
        }

        if ($registered && ($now - $lastBeat) >= $heartbeatSec) {
            $client->heartbeat($serviceName, $instanceId);
            $lastBeat = $now;
        }

        if (method_exists($app, 'isRunning') && !$app->isRunning()) {
            break;
        }

        if (method_exists($app, 'wait')) {
            $app->wait(250);
        } else {
            usleep(250000);
        }
    }
}
