# Standalone Registry

This folder provides a separate service-registry deployment pattern:

1. Run registry as its own process
2. Services self-register and heartbeat
3. Gateway resolves service URLs from registry

## Files

- `registry_server.php` - standalone discovery registry server
- `HttpDiscoveryClient.php` - HTTP client implementing Discovery `ClientInterface`
- `service_example.php` - sample microservice with self-registration + heartbeat
- `gateway_example.php` - gateway resolving services from remote registry

## Prerequisites

- `kislayphp/core` extension loaded
- `kislayphp/discovery` extension loaded
- `kislayphp/gateway` extension loaded (for gateway example)

## 1) Start Registry Server

```bash
REGISTRY_HOST=0.0.0.0 REGISTRY_PORT=9090 php registry_server.php
```

Health check:

```bash
curl -sS http://127.0.0.1:9090/health
```

## 2) Start Services (Self-register)

Service A:

```bash
REGISTRY_URL=http://127.0.0.1:9090 \
SERVICE_NAME=user-service \
SERVICE_PORT=9101 \
SERVICE_URL=http://127.0.0.1:9101 \
SERVICE_ROUTE=/api/users \
INSTANCE_ID=user-1 \
php service_example.php
```

Service B:

```bash
REGISTRY_URL=http://127.0.0.1:9090 \
SERVICE_NAME=order-service \
SERVICE_PORT=9102 \
SERVICE_URL=http://127.0.0.1:9102 \
SERVICE_ROUTE=/api/orders \
INSTANCE_ID=order-1 \
php service_example.php
```

## 3) Start Gateway (Consume Registry)

```bash
REGISTRY_URL=http://127.0.0.1:9090 GATEWAY_PORT=9008 php gateway_example.php
```

## 4) Test Through Gateway

```bash
curl -i http://127.0.0.1:9008/api/users
curl -i http://127.0.0.1:9008/api/orders
```

## Notes

- `gateway_example.php` intentionally uses `setResolver(...)` and `addServiceRoute(...)` to map logical service names to real URLs from registry.
- Resolver now uses cached/optional fallback target instead of throwing, so temporary registry misses do not crash gateway.
- Optional fallback target: `GATEWAY_FALLBACK_TARGET=http://127.0.0.1:9101`
- On NTS PHP builds, `setThreads(4)` in gateway will auto-fallback to `1` with warning.
- For real deployment, set `SERVICE_URL` to the actual reachable host/IP for each service.

For a full content platform example with 4 services (`docs`, `blog`, `community`, `auth`), use `../website_backend/`.
