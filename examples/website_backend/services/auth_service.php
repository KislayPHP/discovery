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

$serviceName = getenv('SERVICE_NAME') ?: 'auth-service';
$serviceHost = getenv('SERVICE_HOST') ?: '0.0.0.0';
$servicePort = (int) (getenv('SERVICE_PORT') ?: '9104');
if ($servicePort < 1 || $servicePort > 65535) {
    $servicePort = 9104;
}
$publicHost = getenv('SERVICE_PUBLIC_HOST') ?: '127.0.0.1';
$serviceUrl = getenv('SERVICE_URL') ?: "http://{$publicHost}:{$servicePort}";
$instanceId = getenv('INSTANCE_ID') ?: ($serviceName . '-' . getmypid());
$registryUrl = getenv('REGISTRY_URL') ?: 'http://127.0.0.1:9090';
$heartbeatSec = (int) (getenv('HEARTBEAT_SEC') ?: '10');
$tokenTtl = (int) (getenv('AUTH_TOKEN_TTL_SEC') ?: '86400');
if ($tokenTtl < 60) {
    $tokenTtl = 86400;
}

$defaultEmail = trim((string) (getenv('AUTH_DEFAULT_EMAIL') ?: 'admin@skelves.com'));
$defaultPassword = (string) (getenv('AUTH_DEFAULT_PASSWORD') ?: 'kislay123');
$defaultName = trim((string) (getenv('AUTH_DEFAULT_NAME') ?: 'Skelves Admin'));
$defaultId = (string) (getenv('AUTH_DEFAULT_ID') ?: 'admin-1');

$client = new HttpDiscoveryClient($registryUrl);
$metadata = [
    'role' => 'authentication',
    'hostname' => gethostname() ?: 'unknown',
    'startedAt' => (string) time(),
];

/** @var object $app */
$app = new $appClass();
if (method_exists($app, 'setOption')) {
    $app->setOption('num_threads', 1);
    $app->setOption('log', true);
}

$users = [
    $defaultEmail => [
        'id' => $defaultId,
        'email' => $defaultEmail,
        'name' => $defaultName,
        'password' => $defaultPassword,
    ],
];

$sessions = [];

$getTokenFromHeader = static function ($req): ?string {
    $auth = kislay_runtime_header($req, 'authorization');
    if (!is_string($auth) || $auth === '') {
        return null;
    }

    if (stripos($auth, 'Bearer ') === 0) {
        $token = trim(substr($auth, 7));
        return $token !== '' ? $token : null;
    }

    return null;
};

$purgeExpired = static function () use (&$sessions): void {
    $now = time();
    foreach ($sessions as $token => $session) {
        if (!is_array($session)) {
            unset($sessions[$token]);
            continue;
        }
        $expiresAt = isset($session['expiresAt']) ? (int) $session['expiresAt'] : 0;
        if ($expiresAt > 0 && $expiresAt <= $now) {
            unset($sessions[$token]);
        }
    }
};

$app->get('/health', function ($req, $res) use ($serviceName, $instanceId) {
    $res->json(['ok' => true, 'service' => $serviceName, 'instanceId' => $instanceId], 200);
});

$app->post('/api/auth/login', function ($req, $res) use (&$sessions, $users, $tokenTtl, $purgeExpired) {
    $purgeExpired();
    $payload = kislay_runtime_read_json($req);
    $email = isset($payload['email']) ? strtolower(trim((string) $payload['email'])) : '';
    $password = isset($payload['password']) ? (string) $payload['password'] : '';

    if ($email === '' || $password === '') {
        $res->json(['ok' => false, 'error' => 'email and password are required'], 400);
        return;
    }

    if (!isset($users[$email]) || !is_array($users[$email]) || $users[$email]['password'] !== $password) {
        $res->json(['ok' => false, 'error' => 'invalid credentials'], 401);
        return;
    }

    $user = $users[$email];
    $token = bin2hex(random_bytes(24));
    $expiresAt = time() + $tokenTtl;

    $sessions[$token] = [
        'user' => [
            'id' => $user['id'],
            'email' => $user['email'],
            'name' => $user['name'],
        ],
        'issuedAt' => time(),
        'expiresAt' => $expiresAt,
    ];

    $res->json([
        'ok' => true,
        'tokenType' => 'Bearer',
        'accessToken' => $token,
        'expiresIn' => $tokenTtl,
        'user' => $sessions[$token]['user'],
    ], 200);
});

$app->get('/api/auth/me', function ($req, $res) use (&$sessions, $getTokenFromHeader, $purgeExpired) {
    $purgeExpired();
    $token = $getTokenFromHeader($req);
    if (!is_string($token) || $token === '' || !isset($sessions[$token])) {
        $res->json(['ok' => false, 'error' => 'unauthorized'], 401);
        return;
    }

    $session = $sessions[$token];
    $res->json([
        'ok' => true,
        'user' => $session['user'] ?? new stdClass(),
        'expiresAt' => $session['expiresAt'] ?? null,
    ], 200);
});

$app->post('/api/auth/logout', function ($req, $res) use (&$sessions, $getTokenFromHeader) {
    $token = $getTokenFromHeader($req);
    if (is_string($token) && $token !== '' && isset($sessions[$token])) {
        unset($sessions[$token]);
    }
    $res->json(['ok' => true], 200);
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
