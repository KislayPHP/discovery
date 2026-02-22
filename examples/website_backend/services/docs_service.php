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

$serviceName = getenv('SERVICE_NAME') ?: 'docs-service';
$serviceHost = getenv('SERVICE_HOST') ?: '0.0.0.0';
$servicePort = (int) (getenv('SERVICE_PORT') ?: '9101');
if ($servicePort < 1 || $servicePort > 65535) {
    $servicePort = 9101;
}
$publicHost = getenv('SERVICE_PUBLIC_HOST') ?: '127.0.0.1';
$serviceUrl = getenv('SERVICE_URL') ?: "http://{$publicHost}:{$servicePort}";
$instanceId = getenv('INSTANCE_ID') ?: ($serviceName . '-' . getmypid());
$registryUrl = getenv('REGISTRY_URL') ?: 'http://127.0.0.1:9090';
$heartbeatSec = (int) (getenv('HEARTBEAT_SEC') ?: '10');

$contentFile = getenv('SITE_CONTENT_FILE') ?: dirname(__DIR__) . '/data/site.json';

$client = new HttpDiscoveryClient($registryUrl);
$metadata = [
    'role' => 'docs-content',
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

$app->get('/api/health', function ($req, $res) use ($serviceName, $instanceId) {
    $res->json(['ok' => true, 'service' => $serviceName, 'instanceId' => $instanceId], 200);
});

$app->get('/api/site/home', function ($req, $res) use ($getDataset) {
    $dataset = $getDataset();
    $res->json(['ok' => true, 'home' => $dataset['home'] ?? new stdClass()], 200);
});

$app->get('/api/site/docs', function ($req, $res) use ($getDataset) {
    $dataset = $getDataset();
    $docs = is_array($dataset['docs'] ?? null) ? $dataset['docs'] : [];
    $res->json([
        'ok' => true,
        'docs' => ['sections' => is_array($docs['sections'] ?? null) ? $docs['sections'] : []],
    ], 200);
});

$app->get('/api/site/kislayphp', function ($req, $res) use ($getDataset) {
    $dataset = $getDataset();
    $page = is_array($dataset['kislayPhp'] ?? null) ? $dataset['kislayPhp'] : [];
    $res->json([
        'ok' => true,
        'kislayPhp' => [
            'hero' => is_array($page['hero'] ?? null) ? $page['hero'] : [],
            'quickInstall' => is_array($page['quickInstall'] ?? null) ? $page['quickInstall'] : [],
            'codeExample' => is_string($page['codeExample'] ?? null) ? $page['codeExample'] : '',
            'featureTabs' => is_array($page['featureTabs'] ?? null) ? $page['featureTabs'] : [],
            'repos' => is_array($page['repos'] ?? null) ? $page['repos'] : [],
        ],
    ], 200);
});

$app->get('/api/site/resources', function ($req, $res) use ($getDataset) {
    $dataset = $getDataset();
    $resources = is_array($dataset['resources'] ?? null) ? $dataset['resources'] : [];
    $res->json([
        'ok' => true,
        'resources' => [
            'tutorials' => is_array($resources['tutorials'] ?? null) ? $resources['tutorials'] : [],
            'examples' => is_array($resources['examples'] ?? null) ? $resources['examples'] : [],
            'videos' => is_array($resources['videos'] ?? null) ? $resources['videos'] : [],
            'tools' => is_array($resources['tools'] ?? null) ? $resources['tools'] : [],
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
