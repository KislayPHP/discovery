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

Install from PIE:

```bash
pie install kislayphp/core
pie install kislayphp/discovery
pie install kislayphp/gateway
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

## Notes

- Services self-register and send heartbeats to registry.
- Gateway resolves service URLs from registry at startup and adds direct routes.
- Optional dynamic resolver mode is available with `GATEWAY_DYNAMIC_RESOLVER=1`.
- On NTS PHP builds, `num_threads` values above `1` auto-fallback to `1` with warning.

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
