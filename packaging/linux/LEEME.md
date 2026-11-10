# Moss @VERSION@ para Linux (x86_64)

Esta carpeta trae todo junto: la interfaz gráfica (`moss-gui`), la de terminal
(`moss`), `moss-rpc` (lo usa la gráfica) y el bot de Telegram (`moss-telegram`).
Se puede poner en cualquier lugar; los programas se buscan entre sí aquí mismo.

## Dependencias del sistema

Fedora:

```bash
sudo dnf install gtk3 libcurl libglvnd-egl libglvnd-gles
```

Debian 12, Ubuntu 22.04 o más nuevos:

```bash
sudo apt install libgtk-3-0 libcurl4 libegl1 libgles2
```

## Correrlo

```bash
./moss-gui          # la interfaz gráfica
./moss              # la de terminal
./moss telegram     # el bot (mejor como servicio: Configuración → Telegram)
```

La configuración y las conversaciones quedan en `~/.config/moss` y
`~/.local/share/moss`, no en esta carpeta.

## En el menú de aplicaciones (opcional)

```bash
mkdir -p ~/.local/bin ~/.local/share
ln -sf "$PWD/moss-gui" "$PWD/moss" ~/.local/bin/
cp -r share/* ~/.local/share/
```

## Si mueves la carpeta con el bot activo

El servicio del bot apunta a la ruta de `moss` donde estaba. Después de
moverla, abre `./moss`, F2 → Addons → Telegram, y guarda (dice "el servicio
está desactualizado: guarda para actualizarlo"); o, en la gráfica,
Configuración → Telegram → Guardar.

## Licencias

`LICENSE` (Moss, GPL v3) y `AVISOS-TERCEROS.txt` (lo de terceros que va adentro).
