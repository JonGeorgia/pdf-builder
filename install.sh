#!/bin/sh
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
APPIMAGE="$DIR/PDFuse-x86_64.AppImage"

if [ ! -x "$APPIMAGE" ]; then
    echo "Ошибка: $APPIMAGE не найден или не исполняемый." >&2
    exit 1
fi

# Копируем иконки из подпапки icons/ (если они там есть)
if [ -d "$DIR/icons" ]; then
    for size in 32 48 256; do
        if [ -f "$DIR/icons/pdfuse-${size}.png" ]; then
            mkdir -p "$HOME/.local/share/icons/hicolor/${size}x${size}/apps"
            cp "$DIR/icons/pdfuse-${size}.png" "$HOME/.local/share/icons/hicolor/${size}x${size}/apps/pdfuse.png"
        fi
    done
fi

# Создаём .desktop с именем иконки, а не с путем
mkdir -p "$HOME/.local/share/applications"
cat > "$HOME/.local/share/applications/pdfuse.desktop" << DESKTOP_EOF
[Desktop Entry]
Type=Application
Name=PDFuse
GenericName=PDF Builder
Comment=Build PDF from Markdown and extract sources back
Exec=$APPIMAGE %U
Icon=pdfuse
Categories=Office;Utility;
Terminal=false
StartupWMClass=PDFuse
DESKTOP_EOF

chmod +x "$HOME/.local/share/applications/pdfuse.desktop"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$HOME/.local/share/applications/" 2>/dev/null || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -f -t "$HOME/.local/share/icons/hicolor/" 2>/dev/null || true

echo "Готово! PDFuse появится в меню приложений."
