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

$storageDir = getenv('SITE_STORAGE_DIR') ?: dirname(__DIR__) . '/data/storage';
$authDbPath = getenv('AUTH_DB_PATH') ?: ($storageDir . '/auth.sqlite');

$authPersistenceConnected = false;
$authPersistenceAttached = false;
$authDb = kislay_runtime_open_sqlite($authDbPath, $app, $authPersistenceConnected, $authPersistenceAttached);
$allowDirectSqlite = kislay_runtime_env_bool('AUTH_ALLOW_DIRECT_SQLITE', false);

if ($authDb instanceof PDO && !$authPersistenceConnected && !$allowDirectSqlite) {
    fwrite(
        STDERR,
        "warning: persistence unavailable for auth DB; using memory storage (set AUTH_ALLOW_DIRECT_SQLITE=1 to force direct PDO)\n"
    );
    $authDb = null;
}

$authStorageMode = 'memory';
$users = [];
$sessions = [];

if ($authDb instanceof PDO) {
    try {
        $authDb->exec(
            'CREATE TABLE IF NOT EXISTS users ('
            . 'id TEXT PRIMARY KEY,'
            . 'email TEXT NOT NULL UNIQUE,'
            . 'name TEXT NOT NULL,'
            . 'password TEXT NOT NULL,'
            . 'created_at INTEGER NOT NULL'
            . ')'
        );
        $authDb->exec(
            'CREATE TABLE IF NOT EXISTS auth_sessions ('
            . 'token TEXT PRIMARY KEY,'
            . 'user_id TEXT NOT NULL,'
            . 'email TEXT NOT NULL,'
            . 'name TEXT NOT NULL,'
            . 'issued_at INTEGER NOT NULL,'
            . 'expires_at INTEGER NOT NULL'
            . ')'
        );
        $authDb->exec('CREATE INDEX IF NOT EXISTS idx_auth_sessions_expires_at ON auth_sessions(expires_at)');

        $seedUser = $authDb->prepare(
            'INSERT OR IGNORE INTO users(id, email, name, password, created_at) '
            . 'VALUES(:id, :email, :name, :password, :created_at)'
        );
        $seedUser->execute([
            ':id' => $defaultId,
            ':email' => $defaultEmail,
            ':name' => $defaultName,
            ':password' => $defaultPassword,
            ':created_at' => time(),
        ]);

        $syncUser = $authDb->prepare('UPDATE users SET id = :id, name = :name WHERE email = :email');
        $syncUser->execute([
            ':id' => $defaultId,
            ':name' => $defaultName,
            ':email' => $defaultEmail,
        ]);

        $authStorageMode = $authPersistenceConnected ? 'sqlite+persistence' : 'sqlite';
    } catch (Throwable $e) {
        fwrite(STDERR, "warning: auth sqlite init failed, using memory storage: {$e->getMessage()}\n");
        $authDb = null;
    }
}

if (!($authDb instanceof PDO)) {
    $users[$defaultEmail] = [
        'id' => $defaultId,
        'email' => $defaultEmail,
        'name' => $defaultName,
        'password' => $defaultPassword,
    ];
}

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

$purgeExpired = static function () use (&$sessions, $authDb): void {
    if ($authDb instanceof PDO) {
        try {
            $stmt = $authDb->prepare('DELETE FROM auth_sessions WHERE expires_at <= :now');
            $stmt->execute([':now' => time()]);
        } catch (Throwable $e) {
            fwrite(STDERR, "warning: auth session purge failed: {$e->getMessage()}\n");
        }
        return;
    }

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

$findUser = static function (string $email, string $password) use (&$users, $authDb): ?array {
    if ($authDb instanceof PDO) {
        try {
            $stmt = $authDb->prepare(
                'SELECT id, email, name, password FROM users WHERE email = :email LIMIT 1'
            );
            $stmt->execute([':email' => $email]);
            $row = $stmt->fetch(PDO::FETCH_ASSOC);
            if (!is_array($row)) {
                return null;
            }

            $expected = (string) ($row['password'] ?? '');
            if ($expected === '' || !hash_equals($expected, $password)) {
                return null;
            }

            return [
                'id' => (string) ($row['id'] ?? ''),
                'email' => (string) ($row['email'] ?? ''),
                'name' => (string) ($row['name'] ?? ''),
            ];
        } catch (Throwable $e) {
            fwrite(STDERR, "warning: auth user lookup failed: {$e->getMessage()}\n");
            return null;
        }
    }

    $record = $users[$email] ?? null;
    if (!is_array($record)) {
        return null;
    }

    $expected = (string) ($record['password'] ?? '');
    if ($expected === '') {
        return null;
    }

    if (!hash_equals($expected, $password)) {
        return null;
    }

    return [
        'id' => (string) ($record['id'] ?? ''),
        'email' => (string) ($record['email'] ?? ''),
        'name' => (string) ($record['name'] ?? ''),
    ];
};

$storeSession = static function (string $token, array $user, int $issuedAt, int $expiresAt) use (&$sessions, $authDb): bool {
    if ($authDb instanceof PDO) {
        try {
            $stmt = $authDb->prepare(
                'INSERT INTO auth_sessions(token, user_id, email, name, issued_at, expires_at) '
                . 'VALUES(:token, :user_id, :email, :name, :issued_at, :expires_at)'
            );
            $stmt->execute([
                ':token' => $token,
                ':user_id' => (string) ($user['id'] ?? ''),
                ':email' => (string) ($user['email'] ?? ''),
                ':name' => (string) ($user['name'] ?? ''),
                ':issued_at' => $issuedAt,
                ':expires_at' => $expiresAt,
            ]);
            return true;
        } catch (Throwable $e) {
            fwrite(STDERR, "warning: auth session create failed: {$e->getMessage()}\n");
            return false;
        }
    }

    $sessions[$token] = [
        'user' => $user,
        'issuedAt' => $issuedAt,
        'expiresAt' => $expiresAt,
    ];
    return true;
};

$readSession = static function (string $token) use (&$sessions, $authDb): ?array {
    if ($authDb instanceof PDO) {
        try {
            $stmt = $authDb->prepare(
                'SELECT user_id, email, name, issued_at, expires_at FROM auth_sessions WHERE token = :token LIMIT 1'
            );
            $stmt->execute([':token' => $token]);
            $row = $stmt->fetch(PDO::FETCH_ASSOC);
            if (!is_array($row)) {
                return null;
            }

            return [
                'user' => [
                    'id' => (string) ($row['user_id'] ?? ''),
                    'email' => (string) ($row['email'] ?? ''),
                    'name' => (string) ($row['name'] ?? ''),
                ],
                'issuedAt' => (int) ($row['issued_at'] ?? 0),
                'expiresAt' => (int) ($row['expires_at'] ?? 0),
            ];
        } catch (Throwable $e) {
            fwrite(STDERR, "warning: auth session lookup failed: {$e->getMessage()}\n");
            return null;
        }
    }

    $session = $sessions[$token] ?? null;
    return is_array($session) ? $session : null;
};

$deleteSession = static function (string $token) use (&$sessions, $authDb): void {
    if ($authDb instanceof PDO) {
        try {
            $stmt = $authDb->prepare('DELETE FROM auth_sessions WHERE token = :token');
            $stmt->execute([':token' => $token]);
        } catch (Throwable $e) {
            fwrite(STDERR, "warning: auth session delete failed: {$e->getMessage()}\n");
        }
        return;
    }

    unset($sessions[$token]);
};

$app->get('/health', function ($req, $res) use (
    $serviceName,
    $instanceId,
    $authStorageMode,
    $authPersistenceConnected,
    $authPersistenceAttached
) {
    $res->json([
        'ok' => true,
        'service' => $serviceName,
        'instanceId' => $instanceId,
        'authStorage' => $authStorageMode,
        'persistenceConnected' => $authPersistenceConnected,
        'persistenceAttached' => $authPersistenceAttached,
    ], 200);
});

$app->post('/api/auth/login', function ($req, $res) use (
    $tokenTtl,
    $purgeExpired,
    $findUser,
    $storeSession
) {
    $purgeExpired();
    $payload = kislay_runtime_read_json($req);
    $email = isset($payload['email']) ? strtolower(trim((string) $payload['email'])) : '';
    $password = isset($payload['password']) ? (string) $payload['password'] : '';

    if ($email === '' || $password === '') {
        $res->json(['ok' => false, 'error' => 'email and password are required'], 400);
        return;
    }

    $user = $findUser($email, $password);
    if (!is_array($user)) {
        $res->json(['ok' => false, 'error' => 'invalid credentials'], 401);
        return;
    }

    $token = bin2hex(random_bytes(24));
    $issuedAt = time();
    $expiresAt = $issuedAt + $tokenTtl;

    if (!$storeSession($token, $user, $issuedAt, $expiresAt)) {
        $res->json(['ok' => false, 'error' => 'session create failed'], 500);
        return;
    }

    $res->json([
        'ok' => true,
        'tokenType' => 'Bearer',
        'accessToken' => $token,
        'expiresIn' => $tokenTtl,
        'user' => $user,
    ], 200);
});

$app->get('/api/auth/me', function ($req, $res) use ($getTokenFromHeader, $purgeExpired, $readSession, $deleteSession) {
    $purgeExpired();
    $token = $getTokenFromHeader($req);
    if (!is_string($token) || $token === '') {
        $res->json(['ok' => false, 'error' => 'unauthorized'], 401);
        return;
    }

    $session = $readSession($token);
    if (!is_array($session)) {
        $res->json(['ok' => false, 'error' => 'unauthorized'], 401);
        return;
    }

    $expiresAt = isset($session['expiresAt']) ? (int) $session['expiresAt'] : 0;
    if ($expiresAt > 0 && $expiresAt <= time()) {
        $deleteSession($token);
        $res->json(['ok' => false, 'error' => 'unauthorized'], 401);
        return;
    }

    $res->json([
        'ok' => true,
        'user' => $session['user'] ?? new stdClass(),
        'expiresAt' => $expiresAt > 0 ? $expiresAt : null,
    ], 200);
});

$app->post('/api/auth/logout', function ($req, $res) use ($getTokenFromHeader, $deleteSession) {
    $token = $getTokenFromHeader($req);
    if (is_string($token) && $token !== '') {
        $deleteSession($token);
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
