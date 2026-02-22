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

$serviceName = getenv('SERVICE_NAME') ?: 'blog-service';
$serviceHost = getenv('SERVICE_HOST') ?: '0.0.0.0';
$servicePort = (int) (getenv('SERVICE_PORT') ?: '9102');
if ($servicePort < 1 || $servicePort > 65535) {
    $servicePort = 9102;
}
$publicHost = getenv('SERVICE_PUBLIC_HOST') ?: '127.0.0.1';
$serviceUrl = getenv('SERVICE_URL') ?: "http://{$publicHost}:{$servicePort}";
$instanceId = getenv('INSTANCE_ID') ?: ($serviceName . '-' . getmypid());
$registryUrl = getenv('REGISTRY_URL') ?: 'http://127.0.0.1:9090';
$heartbeatSec = (int) (getenv('HEARTBEAT_SEC') ?: '10');

$contentFile = getenv('SITE_CONTENT_FILE') ?: dirname(__DIR__) . '/data/site.json';

$client = new HttpDiscoveryClient($registryUrl);
$metadata = [
    'role' => 'blog-content',
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

$app->get('/api/site/blog', function ($req, $res) use ($getDataset) {
    $dataset = $getDataset();
    $blog = is_array($dataset['blog'] ?? null) ? $dataset['blog'] : [];
    $posts = is_array($blog['posts'] ?? null) ? $blog['posts'] : [];
    $category = kislay_runtime_query($req, 'category');

    if (is_string($category) && $category !== '' && $category !== 'All') {
        $posts = array_values(array_filter($posts, static function ($post) use ($category): bool {
            return is_array($post) && isset($post['category']) && $post['category'] === $category;
        }));
    }

    $res->json([
        'ok' => true,
        'blog' => [
            'categories' => is_array($blog['categories'] ?? null) ? $blog['categories'] : ['All'],
            'posts' => $posts,
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
