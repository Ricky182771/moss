# Diseño de la GUI

- `DISENO.md` — la especificación. **Manda sobre las maquetas** si no coinciden.
- `maquetas/` — las pantallas en HTML, tal como salen del canvas de diseño (1440×900). Las etiquetas `<x-dc>`, `<helmet>`, `<sc-if>`, los `{{ … }}` y `support.js` son del editor del canvas: no hacen falta para leerlas. Las fuentes las cargan de Google Fonts.
- `capturas/` — PNG de las maquetas, dibujadas con Geist (1.9.0).
  - `Chat-vacio.png` muestra la ventana de Opciones abierta (llega en G2); `Chat-vacio-sin-opciones.png` es como se ve en G1.
  - `Chat-oscuro.png` muestra el menú `⋯` abierto (llega en G2).

Cada PR de la GUI agrega aquí las maquetas que necesita (Configuración en G3, Telegram en G4, Buscar chats en G2).
