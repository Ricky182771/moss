# Cambios

Cada versión, con lo que trae para quien la instala. De aquí salen las notas
del GitHub Release (`tools/release-notes.sh`, `guias/17-empaquetado.md`). El
detalle de cada hito está en `guias/HISTORIAL.md`.

## 0.1.0

Primera versión con paquetes para Linux (x86_64).

- **Interfaz gráfica** (`moss-gui`): chats, búsqueda de chats, opciones del
  modelo, configuración (proveedor, modelo, búsqueda web, colores y
  accesibilidad) y el bot de Telegram con su registro.
- **Interfaz de terminal** (`moss`): la misma conversación y configuración,
  con F2.
- **Búsqueda web** con Tavily y sus fuentes.
- **Bot de Telegram** (`moss telegram`) como servicio de systemd del usuario,
  configurado desde la app.
- Paquetes: `.rpm` (Fedora), `.deb` (Debian 12, Ubuntu 22.04 o más nuevos) y
  `.tar.gz` (cualquier lugar), con `SHA256SUMS`.
