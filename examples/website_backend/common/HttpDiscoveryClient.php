<?php

declare(strict_types=1);

if (!class_exists('HttpDiscoveryClient')) {
    if (interface_exists('Kislay\\Discovery\\ClientInterface')) {
        abstract class _KislayDiscoveryClientBase implements \Kislay\Discovery\ClientInterface {}
    } elseif (interface_exists('KislayPHP\\Discovery\\ClientInterface')) {
        abstract class _KislayDiscoveryClientBase implements \KislayPHP\Discovery\ClientInterface {}
    } else {
        throw new RuntimeException('Discovery ClientInterface is not available. Load kislayphp_discovery extension first.');
    }

    final class HttpDiscoveryClient extends _KislayDiscoveryClientBase {
        private string $baseUrl;
        private int $timeoutMs;

        public function __construct(string $baseUrl, int $timeoutMs = 2000) {
            $this->baseUrl = rtrim($baseUrl, '/');
            $this->timeoutMs = $timeoutMs > 0 ? $timeoutMs : 2000;
        }

        public function register(string $name, string $url): bool {
            return $this->registerInstance($name, $url, [], sha1($url));
        }

        public function deregister(string $name): bool {
            [$status, $payload] = $this->request('POST', '/v1/deregister', [
                'service' => $name,
            ]);
            return $status >= 200 && $status < 300 && !empty($payload['ok']);
        }

        public function resolve(string $name): ?string {
            $query = http_build_query(['service' => $name]);
            [$status, $payload] = $this->request('GET', '/v1/resolve?' . $query, null);
            if ($status >= 200 && $status < 300 && isset($payload['url']) && is_string($payload['url'])) {
                return $payload['url'];
            }
            return null;
        }

        public function list(): array {
            [$status, $payload] = $this->request('GET', '/v1/services', null);
            if ($status >= 200 && $status < 300 && isset($payload['services']) && is_array($payload['services'])) {
                return $payload['services'];
            }
            return [];
        }

        public function registerInstance(string $service, string $url, array $metadata = [], ?string $instanceId = null): bool {
            [$status, $payload] = $this->request('POST', '/v1/register', [
                'service' => $service,
                'url' => $url,
                'instanceId' => $instanceId ?: sha1($url),
                'metadata' => $metadata,
            ]);
            return $status >= 200 && $status < 300 && !empty($payload['ok']);
        }

        public function deregisterInstance(string $name, ?string $instanceId = null): bool {
            [$status, $payload] = $this->request('POST', '/v1/deregister', [
                'service' => $name,
                'instanceId' => $instanceId,
            ]);
            return $status >= 200 && $status < 300 && !empty($payload['ok']);
        }

        public function heartbeat(string $service, ?string $instanceId = null): bool {
            [$status, $payload] = $this->request('POST', '/v1/heartbeat', [
                'service' => $service,
                'instanceId' => $instanceId,
            ]);
            return $status >= 200 && $status < 300 && !empty($payload['ok']);
        }

        public function setStatus(string $service, string $status, ?string $instanceId = null): bool {
            [$httpStatus, $payload] = $this->request('POST', '/v1/status', [
                'service' => $service,
                'status' => $status,
                'instanceId' => $instanceId,
            ]);
            return $httpStatus >= 200 && $httpStatus < 300 && !empty($payload['ok']);
        }

        public function listInstances(string $service): array {
            $query = http_build_query(['service' => $service]);
            [$status, $payload] = $this->request('GET', '/v1/instances?' . $query, null);
            if ($status >= 200 && $status < 300 && isset($payload['instances']) && is_array($payload['instances'])) {
                return $payload['instances'];
            }
            return [];
        }

        /** @return array{0:int,1:array<string,mixed>} */
        private function request(string $method, string $path, ?array $body): array {
            $url = $this->baseUrl . $path;
            $rawBody = $body === null ? null : json_encode($body);
            if ($rawBody === false) {
                return [500, ['ok' => false, 'error' => 'json_encode failed']];
            }

            if (function_exists('curl_init')) {
                $ch = curl_init($url);
                if ($ch === false) {
                    return [500, ['ok' => false, 'error' => 'curl_init failed']];
                }
                curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
                curl_setopt($ch, CURLOPT_CUSTOMREQUEST, $method);
                curl_setopt($ch, CURLOPT_CONNECTTIMEOUT_MS, $this->timeoutMs);
                curl_setopt($ch, CURLOPT_TIMEOUT_MS, $this->timeoutMs);
                curl_setopt($ch, CURLOPT_HTTPHEADER, ['Content-Type: application/json']);
                if ($rawBody !== null) {
                    curl_setopt($ch, CURLOPT_POSTFIELDS, $rawBody);
                }
                $response = curl_exec($ch);
                $status = (int) curl_getinfo($ch, CURLINFO_RESPONSE_CODE);
                // curl_close() is deprecated/no-op on modern PHP; avoid deprecation noise.
                if (!is_string($response)) {
                    return [$status > 0 ? $status : 503, ['ok' => false, 'error' => 'request failed']];
                }
                $decoded = json_decode($response, true);
                if (is_array($decoded)) {
                    return [$status, $decoded];
                }
                return [$status, ['ok' => false, 'error' => 'invalid json response']];
            }

            $context = stream_context_create([
                'http' => [
                    'method' => $method,
                    'header' => "Content-Type: application/json\r\n",
                    'content' => $rawBody ?? '',
                    'timeout' => $this->timeoutMs / 1000,
                    'ignore_errors' => true,
                ],
            ]);

            $response = @file_get_contents($url, false, $context);
            $status = 0;
            $headers = [];
            if (function_exists('http_get_last_response_headers')) {
                $last = http_get_last_response_headers();
                if (is_array($last)) {
                    $headers = array_values($last);
                }
            }
            if (isset($headers[0]) && preg_match('/\s(\d{3})\s/', $headers[0], $matches) === 1) {
                $status = (int) $matches[1];
            }

            if (!is_string($response)) {
                return [$status > 0 ? $status : 503, ['ok' => false, 'error' => 'request failed']];
            }

            $decoded = json_decode($response, true);
            if (is_array($decoded)) {
                return [$status, $decoded];
            }
            return [$status, ['ok' => false, 'error' => 'invalid json response']];
        }
    }
}
