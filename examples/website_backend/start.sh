#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PID_DIR="$ROOT/pids"
LOG_DIR="$ROOT/logs"
mkdir -p "$PID_DIR" "$LOG_DIR"

PHP_BIN="${PHP_BIN:-php}"

REGISTRY_HOST="${REGISTRY_HOST:-0.0.0.0}"
REGISTRY_PORT="${REGISTRY_PORT:-9090}"
REGISTRY_URL="${REGISTRY_URL:-http://127.0.0.1:${REGISTRY_PORT}}"

SERVICE_PUBLIC_HOST="${SERVICE_PUBLIC_HOST:-127.0.0.1}"
DOCS_SERVICE_PORT="${DOCS_SERVICE_PORT:-9101}"
BLOG_SERVICE_PORT="${BLOG_SERVICE_PORT:-9102}"
COMMUNITY_SERVICE_PORT="${COMMUNITY_SERVICE_PORT:-9103}"
AUTH_SERVICE_PORT="${AUTH_SERVICE_PORT:-9104}"

GATEWAY_HOST="${GATEWAY_HOST:-0.0.0.0}"
GATEWAY_PORT="${GATEWAY_PORT:-9008}"

ensure_port_free() {
  local port="$1"
  if lsof -nP -iTCP:"${port}" -sTCP:LISTEN >/dev/null 2>&1; then
    echo "Port ${port} is already in use. Stop existing process first."
    exit 1
  fi
}

start_proc() {
  local name="$1"
  local cmd="$2"
  local pid_file="$PID_DIR/${name}.pid"
  local log_file="$LOG_DIR/${name}.log"

  if [[ -f "$pid_file" ]] && kill -0 "$(cat "$pid_file")" 2>/dev/null; then
    echo "${name} already running with PID $(cat "$pid_file")"
    return
  fi

  nohup bash -lc "$cmd" >"$log_file" 2>&1 &
  local pid=$!
  echo "$pid" > "$pid_file"
  sleep 0.3
  if ! kill -0 "$pid" 2>/dev/null; then
    echo "Failed to start ${name}. Check ${log_file}"
    exit 1
  fi
  echo "Started ${name} (PID ${pid})"
}

ensure_port_free "$REGISTRY_PORT"
ensure_port_free "$DOCS_SERVICE_PORT"
ensure_port_free "$BLOG_SERVICE_PORT"
ensure_port_free "$COMMUNITY_SERVICE_PORT"
ensure_port_free "$AUTH_SERVICE_PORT"
ensure_port_free "$GATEWAY_PORT"

start_proc "registry" \
  "REGISTRY_HOST=${REGISTRY_HOST} REGISTRY_PORT=${REGISTRY_PORT} ${PHP_BIN} '${ROOT}/registry/registry_server.php'"

start_proc "docs_service" \
  "REGISTRY_URL=${REGISTRY_URL} SERVICE_NAME=docs-service SERVICE_PORT=${DOCS_SERVICE_PORT} SERVICE_URL=http://${SERVICE_PUBLIC_HOST}:${DOCS_SERVICE_PORT} ${PHP_BIN} '${ROOT}/services/docs_service.php'"

start_proc "blog_service" \
  "REGISTRY_URL=${REGISTRY_URL} SERVICE_NAME=blog-service SERVICE_PORT=${BLOG_SERVICE_PORT} SERVICE_URL=http://${SERVICE_PUBLIC_HOST}:${BLOG_SERVICE_PORT} ${PHP_BIN} '${ROOT}/services/blog_service.php'"

start_proc "community_service" \
  "REGISTRY_URL=${REGISTRY_URL} SERVICE_NAME=community-service SERVICE_PORT=${COMMUNITY_SERVICE_PORT} SERVICE_URL=http://${SERVICE_PUBLIC_HOST}:${COMMUNITY_SERVICE_PORT} ${PHP_BIN} '${ROOT}/services/community_service.php'"

start_proc "auth_service" \
  "REGISTRY_URL=${REGISTRY_URL} SERVICE_NAME=auth-service SERVICE_PORT=${AUTH_SERVICE_PORT} SERVICE_URL=http://${SERVICE_PUBLIC_HOST}:${AUTH_SERVICE_PORT} ${PHP_BIN} '${ROOT}/services/auth_service.php'"

start_proc "gateway" \
  "REGISTRY_URL=${REGISTRY_URL} GATEWAY_HOST=${GATEWAY_HOST} GATEWAY_PORT=${GATEWAY_PORT} ${PHP_BIN} '${ROOT}/gateway/gateway_server.php'"

"$ROOT/status.sh"
