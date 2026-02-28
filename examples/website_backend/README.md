# Website Backend Example (Discovery + Gateway)

This example runs 4 independent backend services for a content platform:

- docs
- blog
- community
- authentication

All traffic goes through one gateway port (`9008`). Services self-register in discovery registry.

## Services

- `docs-service` (`9101`): `/api/site/home`, `/api/site/docs`, `/api/site/kislayphp`, `/api/site/resources`
- `blog-service` (`9102`): `/api/site/blog`
- `community-service` (`9103`): `/api/site/community`
- `auth-service` (`9104`): `/api/auth/login`, `/api/auth/me`, `/api/auth/logout`

## Infrastructure

- Registry: `9090`
- Gateway: `9008`

Frontend should call gateway (`/api/...`) and does not need to know service ports.

## Prerequisites

- `kislayphp/core` loaded
- `kislayphp/discovery` loaded
- `kislayphp/gateway` loaded
- `kislayphp/persistence` optional (enables SQLite + request-lifecycle cleanup for auth sessions)

Install from PIE:

```bash
pie install kislayphp/core
pie install kislayphp/discovery:0.0.3
pie install kislayphp/gateway
pie install kislayphp/persistence:0.0.1
```

## Start

```bash
cd examples/website_backend
./start.sh
```

## Status

```bash
./status.sh
```

## Smoke Test

```bash
./smoke_test.sh
```

### Reproducible Local Validation (custom ports)

If default ports are already in use, run with isolated ports:

```bash
REGISTRY_PORT=19090 \
GATEWAY_PORT=19008 \
DOCS_SERVICE_PORT=19101 \
BLOG_SERVICE_PORT=19102 \
COMMUNITY_SERVICE_PORT=19103 \
AUTH_SERVICE_PORT=19104 \
./start.sh

GATEWAY_PORT=19008 ./smoke_test.sh
./stop.sh
```

If your local default `php.ini` contains stale extension entries, set `PHP_BIN` explicitly:

```bash
PHP_BIN='php -n -dextension=/absolute/path/kislayphp_extension.so -dextension=/absolute/path/kislayphp_discovery.so -dextension=/absolute/path/kislayphp_gateway.so' \
REGISTRY_PORT=19090 GATEWAY_PORT=19008 DOCS_SERVICE_PORT=19101 BLOG_SERVICE_PORT=19102 COMMUNITY_SERVICE_PORT=19103 AUTH_SERVICE_PORT=19104 \
./start.sh
```

## Stop

```bash
./stop.sh
```

## Key Environment Variables

- `REGISTRY_HOST` (default `0.0.0.0`)
- `REGISTRY_PORT` (default `9090`)
- `REGISTRY_URL` (default `http://127.0.0.1:9090`)
- `GATEWAY_HOST` (default `0.0.0.0`)
- `GATEWAY_PORT` (default `9008`)
- `GATEWAY_DYNAMIC_RESOLVER` (default `0`; set `1` to use per-request resolver callback)
- `SERVICE_PUBLIC_HOST` (default `127.0.0.1`)
- `DOCS_SERVICE_PORT` (default `9101`)
- `BLOG_SERVICE_PORT` (default `9102`)
- `COMMUNITY_SERVICE_PORT` (default `9103`)
- `AUTH_SERVICE_PORT` (default `9104`)
- `KISLAY_USE_PERSISTENCE` (default `1`; set `0` to force direct PDO/memory fallback)
- `SITE_STORAGE_DIR` (default `examples/website_backend/data/storage`)
- `AUTH_DB_PATH` (default `${SITE_STORAGE_DIR}/auth.sqlite`)
- `AUTH_ALLOW_DIRECT_SQLITE` (default `0`; keep `0` for async safety fallback to memory when persistence is unavailable)

## Notes

- Services self-register and send heartbeats to registry.
- Gateway resolves service URLs from registry at startup and adds direct routes.
- Optional dynamic resolver mode is available with `GATEWAY_DYNAMIC_RESOLVER=1`.
- On NTS PHP builds, `num_threads` values above `1` auto-fallback to `1` with warning.
- Auth service stores users/sessions in SQLite only when `Kislay\Persistence\DB` is active; otherwise it falls back to in-memory auth state by default.

## Manual Test URLs

- Home: `http://127.0.0.1:9008/api/site/home`
- Docs: `http://127.0.0.1:9008/api/site/docs`
- Blog: `http://127.0.0.1:9008/api/site/blog`
- Community: `http://127.0.0.1:9008/api/site/community`

Login:

```bash
curl -sS -X POST http://127.0.0.1:9008/api/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"email":"admin@skelves.com","password":"kislay123"}'
```
