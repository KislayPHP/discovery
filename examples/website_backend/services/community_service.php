<?php

declare(strict_types=1);

require __DIR__ . '/../common/runtime.php';
require __DIR__ . '/../common/HttpDiscoveryClient.php';

try {
    $appClass = kislay_runtime_app_class();
} catch (Throwable $e) {
    fwrite(STDERR, $e->getMessage() . "\n");
    exit(1);
}

$serviceName = getenv('SERVICE_NAME') ?: 'community-service';
$serviceHost = getenv('SERVICE_HOST') ?: '0.0.0.0';
$servicePort = (int) (getenv('SERVICE_PORT') ?: '9103');
if ($servicePort < 1 || $servicePort > 65535) {
    $servicePort = 9103;
}
$publicHost = getenv('SERVICE_PUBLIC_HOST') ?: '127.0.0.1';
$serviceUrl = getenv('SERVICE_URL') ?: "http://{$publicHost}:{$servicePort}";
$instanceId = getenv('INSTANCE_ID') ?: ($serviceName . '-' . getmypid());
$registryUrl = getenv('REGISTRY_URL') ?: 'http://127.0.0.1:9090';
$heartbeatSec = (int) (getenv('HEARTBEAT_SEC') ?: '10');

$contentFile = getenv('SITE_CONTENT_FILE') ?: dirname(__DIR__) . '/data/site.json';

$client = new HttpDiscoveryClient($registryUrl);
$metadata = [
    'role' => 'community-content',
    'hostname' => gethostname() ?: 'unknown',
    'startedAt' => (string) time(),
];

/** @var object $app */
$app = new $appClass();
if (method_exists($app, 'setOption')) {
    $app->setOption('num_threads', 1);
    $app->setOption('log', true);
}

$getDataset = static function () use ($contentFile): array {
    return kislay_runtime_load_dataset($contentFile);
};

$app->get('/health', function ($req, $res) use ($serviceName, $instanceId) {
    $res->json(['ok' => true, 'service' => $serviceName, 'instanceId' => $instanceId], 200);
});

$app->get('/api/site/community', function ($req, $res) use ($getDataset) {
    $dataset = $getDataset();
    $community = is_array($dataset['community'] ?? null) ? $dataset['community'] : [];

    $res->json([
        'ok' => true,
        'community' => [
            'stats' => is_array($community['stats'] ?? null) ? $community['stats'] : [],
            'topContributors' => is_array($community['topContributors'] ?? null) ? $community['topContributors'] : [],
            'upcomingEvents' => is_array($community['upcomingEvents'] ?? null) ? $community['upcomingEvents'] : [],
            'forumTopics' => is_array($community['forumTopics'] ?? null) ? $community['forumTopics'] : [],
        ],
    ], 200);
});

kislay_runtime_start_service(
    $app,
    $client,
    $serviceName,
    $serviceHost,
    $servicePort,
    $serviceUrl,
    $instanceId,
    $metadata,
    $heartbeatSec
);
