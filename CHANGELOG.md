# Cambios

Cada versión, con lo que trae para quien la instala. De aquí salen las notas
del GitHub Release (`tools/release-notes.sh`, `guias/17-empaquetado.md`). El
detalle de cada hito está en `guias/HISTORIAL.md`.

## 0.1.0

Primera versión de Moss: un asistente con el que platicas por escrito, con
interfaz gráfica y de terminal, para Linux y Windows. Usa el proveedor de
inteligencia artificial que tú elijas (por ejemplo, NVIDIA NIM, con su plan
gratuito) con tu propia API key.

- **Chat:** la respuesta aparece mientras llega, se puede detener, y en los
  modelos que razonan se ve lo que van pensando. Markdown y bloques de
  código con Copiar y Guardar.
- **Tus chats**, guardados en tu computadora: buscar, renombrar, exportar y
  borrar.
- **Búsqueda web** con Tavily (con tu key): responde con las fuentes.
- **Configuración:** proveedor y modelo (cualquiera compatible con la API de
  OpenAI), ajustes del modelo, instrucciones del sistema, y colores y
  accesibilidad (tema, tamaño del texto, más contraste y menos animaciones).
- **Interfaz de terminal** (`moss`): lo mismo, en la terminal.
- **Modo portable** en Windows: todo en la carpeta `datos`, para escuelas o
  una memoria USB.
- **Bot de Telegram** (solo Linux): Moss en Telegram, como servicio, con
  acceso por invitación y límites diarios, configurado desde la app.

Descargas: `.rpm` (Fedora), `.deb` (Debian 12, Ubuntu 22.04 o más nuevos) y
`.tar.gz` para Linux x86_64; ZIP para Windows 10 y 11 de 64 bits. Con
`SHA256SUMS`.

Lo que todavía falta:

- **Windows sin firma digital:** al abrir Moss, SmartScreen avisa ("Windows
  protegió su PC") y en Windows 11 el Control Inteligente de Aplicaciones lo
  bloquea. El `LEEME.txt` del ZIP explica qué hacer. Estamos tramitando la
  firma.
- En modo portable, la key queda sin cifrar en la carpeta `datos`.
- Sin versiones para ARM ni macOS.
