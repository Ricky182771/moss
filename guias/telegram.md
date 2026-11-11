# Bot de Telegram

Cómo crear, configurar y mantener el bot de Telegram de Moss (solo Linux). Las decisiones de diseño y las reglas del addon están en [14-addons.md](14-addons.md).

`moss-telegram` es un bot de Telegram que responde con el mismo modelo y las mismas instrucciones que Moss de la terminal. Usa *long polling* (solo hace peticiones HTTPS de salida, sin servidor público ni certificados), responde **solo en chats privados** (en grupos y canales no contesta) y las conversaciones viven **solo en memoria**: se borran tras un rato sin mensajes, con `/nuevo` o al reiniciar el bot. Solo Linux por ahora.

Mientras el modelo escribe, el alumno ve la respuesta crecer en una vista previa con un botón para **detenerla**; al terminar llega como un mensaje con formato (encabezados, listas, tablas, código, fórmulas). Si Telegram no acepta el formato, llega como texto plano. Las respuestas muy largas se parten en varios mensajes.

> **Privacidad y edad.** El bot no guarda conversaciones ni nombres; solo los IDs de quienes entran con el código de invitación. La edad mínima para usarlo, qué se le dice a las familias y quién administra el bot se acuerdan con la escuela antes de dárselo a los alumnos.

## Crear el bot

1. En Telegram, abre [@BotFather](https://t.me/BotFather), manda `/newbot` y sigue los pasos (nombre y usuario terminado en `bot`). Te da el **token**, algo como `123456789:AA...`. Es una contraseña: no lo compartas ni lo subas al repositorio.
2. Recomendado: en @BotFather, `/setjoingroups` → *Disable*, para que nadie pueda meter al bot en un grupo.
3. Pide para el bot **su propia key** del proveedor del modelo (no la tuya), con su propio tope diario de gasto: así un mal uso del bot no te deja sin servicio y el gasto queda acotado. Lo mismo con la key de búsqueda (Tavily) si vas a usar `/buscar`. Si no le pones keys propias, el bot usa las de la app.

## Instalar o quitar el addon

Los paquetes de Linux (`.rpm`, `.deb` y `.tar.gz`) ya lo traen ([instalar.md](instalar.md#linux)). En Linux, cualquier preset compila el bot junto con la app: `moss-telegram` queda junto a `moss`, en `build/<preset>/cli/`.

```bash
cmake --preset release && cmake --build --preset release
./build/release/cli/moss --help      # dice si el addon está instalado
```

> **Si ya tenías `build/<preset>/` de antes**, CMake recuerda las opciones de entonces y puede seguir sin compilar el bot: borra la carpeta (`rm -rf build/<preset>`) o pasa `-DCHATBOT_BUILD_TELEGRAM=ON` una vez.

El addon es solo el programa `moss-telegram`. `moss telegram` lo busca **junto a `moss`** y después en el `PATH` (en [modo portable](configurar.md#modo-portable), solo junto a `moss`). Para instalarlo en una app compilada sin él (`-DCHATBOT_BUILD_TELEGRAM=OFF`), copia `moss-telegram` de otro build junto a `moss`, sin recompilar nada. Para quitarlo, bórralo: F2 dirá "No instalado" y el servicio sale con 69 sin reintentar. Si la app y el bot no entienden la misma versión de la configuración, F2 dice "Incompatible" y muestra los campos sin dejar editarlos.

## Configurar desde F2 → Addons

Todo se configura en la app, sin editar archivos: **F2 → Addons → Telegram**. El bot usa el mismo `config.json` y el mismo `credentials.json` que la app (sección "addons" → "telegram" y llaves `telegram`, `telegram:api_key` y `telegram:search_key`), y también el mismo proveedor, URL base, instrucciones del sistema y límite del historial. Solo el modelo puede ser otro.

Arriba se ve si el addon está instalado, cómo está el servicio (y su último error si no corre), los botones `[Reiniciar]` y `[Actualizar estado]`, `[Ver registro]`, el aviso de linger y el comando del registro. Debajo, los campos (cada uno dice su error debajo, en rojo):

| Campo | Qué es | Por defecto |
|-------|--------|-------------|
| Activado | Si el servicio corre. | apagado |
| Token | El de @BotFather. Se ve enmascarado (`guardada: …AbCd`); vacío = se conserva el guardado. | — |
| Modelo | Vacío: el de la app. | el de la app |
| Key del modelo, Key de búsqueda | Keys propias del bot. `[Quitar]` las borra y el bot vuelve a usar las de la app. | las de la app |
| IDs autorizados | IDs de Telegram separados por comas. | ninguno |
| Código de invitación | Para entrar con `/start <código>` (de 8 a 64 letras sin acento, números, `_` o `-`). `[Generar]` hace uno al azar. Vacío: sin código. | sin código |
| Mensajes por día | Mensajes al modelo por alumno por día (1 a 10000). | 50 |
| Respuestas a la vez | En todo el bot (1 a 32); las demás esperan su turno. | 4 |
| Min. de inactividad | Minutos sin mensajes para borrar una conversación (1 a 1440). | 60 |
| Min. mensaje viejo | Un mensaje que llega con más de estos minutos (el bot estuvo apagado) no va al modelo: el bot pide mandarlo otra vez. | 10 |
| Vista previa (ms) | Cada cuánto se actualiza la vista previa, como mucho (250 a 10000). | 1000 |
| Zona horaria | Cambio de día del tope diario: un nombre de IANA (`America/Tijuana`) o un desfase fijo (`-06:00`). | `America/Mexico_City` |
| Detalle de errores en el registro | El registro guarda el mensaje de error del servidor, que puede citar lo que escribió el alumno. Solo para diagnosticar. | apagado |

Las variables `CHAT_TELEGRAM_TOKEN`, `CHAT_API_KEY` y `CHAT_SEARCH_API_KEY` ganan sobre `credentials.json`, pero el servicio no ve las de tu terminal: si están definidas, F2 lo avisa y no bloquea ningún campo.

**Guardar** escribe la configuración y deja el servicio como dice **Activado**, aunque no hayas cambiado la casilla, sin cerrar F2 (el resultado sale abajo). Si el servicio no está como dice la casilla, arriba lo avisa (por ejemplo, `Activado, pero el servicio está detenido: guarda para arrancarlo.`):

- **Activado:** si falta el servicio lo instala (si hay uno hecho a mano, primero pregunta `¿Reemplazar el servicio existente? (s/n)`), si se movió `moss` lo actualiza, y si está detenido o deshabilitado lo arranca y lo habilita; si ya corría, le manda la configuración nueva **sin borrar las conversaciones**.
- **Sin Activado:** si corre o está habilitado, lo detiene y lo deshabilita.
- El **token** y **Respuestas a la vez** solo se toman al arrancar: si cambian con el bot corriendo, F2 pregunta si reiniciarlo (eso borra las conversaciones en memoria).
- Las preguntas `(s/n)` solo se contestan con **s** o **n**; **Esc** cancela sin hacer nada.
- `[Reiniciar]` se ve atenuado y no hace nada si Activado no está guardado: dice `Activa el addon y guarda para arrancarlo.`

## Autorizar a los alumnos

Sin IDs autorizados ni código de invitación nadie entra. A quien no tiene acceso, el bot le contesta *"No tienes acceso a este bot."* y **su ID**, para que se lo dé al profe:

- **Lista de IDs:** agrega el ID en F2 y guarda; vale al momento.
- **Código de invitación:** el alumno manda `/start <código>`, o abre el enlace `https://t.me/<usuario_del_bot>?start=<código>`. Su ID se guarda (solo el ID, permisos 600) y sigue entrando después de reiniciar, aunque luego quites el código. F2 → Addons muestra los invitados al final; **Supr** sobre uno lo quita al momento, sin esperar a Guardar, y deja de poder escribir. Si el archivo de invitados está dañado, F2 lo dice en lugar de la lista y el bot no arranca (no lo toca: arréglalo o quítalo).

Comandos: `/start`, `/ayuda`, `/nuevo` (conversación nueva) y `/buscar <consulta>` (si hay key de búsqueda). El bot solo lee texto: a fotos, stickers, audios y archivos responde que por ahora solo puede leer texto.

## El servicio (systemd de usuario)

F2 escribe `~/.config/systemd/user/moss-telegram.service`, que arranca `moss telegram`, y lo controla con `systemctl --user`. Si se cae, systemd lo levanta otra vez a los 10 s, salvo con un error que no se arregla solo (configuración, addon borrado, archivo de invitados dañado, carpeta de datos sin permisos): ahí se queda detenido y F2 dice qué pasó. Sin red no hace falta reiniciarlo: el bot espera y reintenta solo. Después de un apagón, los mensajes que llegaron mientras estaba apagado (Telegram los guarda hasta 24 h) con más de "Min. mensaje viejo" reciben *"Estuve desconectado y se me pasó tu mensaje; mándalo otra vez."*, sin gastar el tope diario.

**Linger.** Sin él, el servicio se detiene al cerrar sesión y no arranca al prender la PC hasta que inicies sesión. F2 avisa si está apagado; actívalo tú una vez (la app nunca lo hace):

```bash
loginctl enable-linger "$USER"
```

El registro (una línea por evento con IDs, tiempos, tamaños y tipos de error; **nunca** el texto de los alumnos, las respuestas, el token ni las keys):

```bash
journalctl --user -u moss-telegram -f
```

O, sin salir de la app, **F2 → Addons → `[Ver registro]`**: las últimas 200 líneas, empezando por la más nueva abajo, con las de error en rojo (rueda, PgUp/PgDn, Inicio/Fin; `[Actualizar]` las vuelve a leer y Esc regresa). Con "Detalle de errores en el registro" apagado no se muestra lo que va desde ` detalle=`. Si un servidor repite la key en su mensaje de error, la app y el bot la cambian por `[key oculta]`.

Por SSH sin sesión gráfica puede no haber systemd de usuario: F2 dice `No hay systemd de usuario en esta sesión.`

También se puede correr a mano, sin servicio, con `./build/release/cli/moss telegram` (Ctrl+C lo apaga en menos de 2 s). Sale con 0 si está desactivado, 65 si el archivo de invitados está dañado, 69 si no encuentra `moss-telegram`, 73 si no puede escribir en su carpeta de datos y 78 si la configuración no sirve (incluido un token inválido); el mensaje dice qué arreglar. Todos los códigos: [14-addons.md](14-addons.md).

## Si venías de `telegram.json`

El bot ya no lee `telegram.json`, `--config` ni `CHAT_TELEGRAM_CONFIG`, y no hay migración. Detén y borra lo de antes, y configura todo otra vez desde F2:

```bash
systemctl --user disable --now moss-telegram     # si tenías el servicio hecho a mano
rm ~/.config/systemd/user/moss-telegram.service
systemctl --user daemon-reload
rm -r ~/.config/moss-telegram                    # telegram.json y su credentials.json
```

Los invitados de antes (`<data_dir>/invitados.json`) no se leen: vuelven a entrar con el código, o pon sus IDs en "IDs autorizados".

## Probar cómo se ven los mensajes

`moss-telegram-probe` manda a un chat unos mensajes de prueba (el signo de pesos, fórmulas, escapes, una vista previa con streaming) para revisar en el celular cómo los muestra Telegram. Solo se corre a mano, con el token de un bot de prueba y tu ID (te lo dice el bot si le escribes sin estar autorizado):

```bash
CHAT_TELEGRAM_TOKEN=123456789:AA... ./build/release/cli/moss-telegram-probe --chat <tu ID>
```

Los mensajes con formato llegaron a Telegram en junio de 2026 (Bot API 10.1); en una app desactualizada podrían no verse bien. Falta probarlo con un celular real.
