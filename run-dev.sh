#!/bin/sh
# Запуск PDFuse в режиме разработки.
# Активирует локальный venv с Python-зависимостями (matplotlib, weasyprint).
DIR="$(cd "$(dirname "$0")" && pwd)"
export PATH="$DIR/venv/bin:$PATH"
exec "$DIR/pdf-builder-c" "$@"
