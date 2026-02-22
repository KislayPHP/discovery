#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PID_DIR="$ROOT/pids"

REGISTRY_PORT="${REGISTRY_PORT:-9090}"
GATEWAY_PORT="${GATEWAY_PORT:-9008}"
DOCS_SERVICE_PORT="${DOCS_SERVICE_PORT:-9101}"
BLOG_SERVICE_PORT="${BLOG_SERVICE_PORT:-9102}"
COMMUNITY_SERVICE_PORT="${COMMUNITY_SERVICE_PORT:-9103}"
AUTH_SERVICE_PORT="${AUTH_SERVICE_PORT:-9104}"

show_proc() {
  local name="$1"
  local pid_file="$PID_DIR/${name}.pid"
  if [[ -f "$pid_file" ]]; then
    local pid
    pid="$(cat "$pid_file")"
    if kill -0 "$pid" 2>/dev/null; then
      echo "${name}: running (PID ${pid})"
    else
      echo "${name}: stale pid file (${pid})"
    fi
  else
    echo "${name}: stopped"
  fi
}

show_port() {
  local name="$1"
  local port="$2"
  if lsof -nP -iTCP:"${port}" -sTCP:LISTEN >/dev/null 2>&1; then
    echo "${name} port ${port}: listening"
  else
    echo "${name} port ${port}: not listening"
  fi
}

show_proc "registry"
show_proc "docs_service"
show_proc "blog_service"
show_proc "community_service"
show_proc "auth_service"
show_proc "gateway"

show_port "registry" "$REGISTRY_PORT"
show_port "docs" "$DOCS_SERVICE_PORT"
show_port "blog" "$BLOG_SERVICE_PORT"
show_port "community" "$COMMUNITY_SERVICE_PORT"
show_port "auth" "$AUTH_SERVICE_PORT"
show_port "gateway" "$GATEWAY_PORT"

curl -sS "http://127.0.0.1:${REGISTRY_PORT}/health" >/dev/null 2>&1 && echo "registry health: ok" || echo "registry health: fail"
curl -sS "http://127.0.0.1:${GATEWAY_PORT}/health" >/dev/null 2>&1 && echo "gateway health: ok" || echo "gateway health: fail"
