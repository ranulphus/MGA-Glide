#!/usr/bin/env bash
# Serve dist/bench over HTTP for the bench PCs' pollers.
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p dist/bench
exec python3 -m http.server "${BENCH_HTTP_PORT:-8000}" --directory dist/bench
