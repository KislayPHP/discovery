#!/usr/bin/env bash
set -euo pipefail

GATEWAY_PORT="${GATEWAY_PORT:-9008}"
BASE="http://127.0.0.1:${GATEWAY_PORT}"

echo "[smoke] GET /api/site/home"
curl -fsS "$BASE/api/site/home" >/dev/null

echo "[smoke] GET /api/site/blog"
curl -fsS "$BASE/api/site/blog" >/dev/null

echo "[smoke] GET /api/site/community"
curl -fsS "$BASE/api/site/community" >/dev/null

echo "[smoke] GET /api/site/docs"
curl -fsS "$BASE/api/site/docs" >/dev/null

echo "[smoke] POST /api/auth/login"
LOGIN_JSON="$(curl -fsS -X POST "$BASE/api/auth/login" -H 'Content-Type: application/json' -d '{"email":"admin@skelves.com","password":"kislay123"}')"
TOKEN="$(printf '%s' "$LOGIN_JSON" | php -r '$d=json_decode(stream_get_contents(STDIN), true); echo is_array($d) && isset($d["accessToken"]) ? $d["accessToken"] : "";')"
if [[ -z "$TOKEN" ]]; then
  echo "[smoke] auth token missing"
  exit 1
fi

echo "[smoke] GET /api/auth/me"
curl -fsS "$BASE/api/auth/me" -H "Authorization: Bearer ${TOKEN}" >/dev/null

echo "[smoke] all checks passed"
