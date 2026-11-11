# Instalar Moss

Los archivos están en la página de [releases](https://github.com/Ricky182771/moss/releases); qué archivo bajar en cada sistema está en el [README](../README.md#descargas). `SHA256SUMS` trae la suma de cada archivo.

## Windows

1. Baja `moss-<versión>-windows-x64.zip` y descomprímelo (clic derecho → "Extraer todo..."). No lo abras desde adentro del ZIP.
2. Abre `moss-gui.exe` con doble clic. `moss.exe` es la versión de terminal; `moss-rpc.exe` lo usa la GUI por detrás y no se abre a mano.

No hace falta instalar nada más: las DLL que necesita van en la carpeta. El `LEEME.txt` del ZIP lo explica para quien nunca ha usado algo así.

- **SmartScreen** ("Windows protegió su PC"): sale porque los binarios todavía no están firmados. "Más información" → "Ejecutar de todas formas"; o, antes de descomprimir, clic derecho en el ZIP → Propiedades → Desbloquear.
- **Control Inteligente de Aplicaciones** (Windows 11): bloquea los programas sin firma que no conoce y no tiene excepción por programa; solo se puede apagar para toda la computadora, y no lo recomendamos. Estamos tramitando la firma (ver [Code signing policy](../README.md#code-signing-policy)).
- **Modo portable** (escuelas, memoria USB): renombra `moss.portable.ejemplo` a `moss.portable` y todo (configuración, key y chats) queda en `datos\`, dentro de la carpeta; ver [Modo portable](configurar.md#modo-portable). La key queda ahí sin cifrar: al terminar en una computadora ajena, borra la carpeta.
- **Desinstalar:** borra la carpeta y, si no usaste el modo portable, `%APPDATA%\moss` y `%LOCALAPPDATA%\moss`.

Cómo se usa en Windows (terminal, pegar, conhost): [uso.md](uso.md#en-windows).

## Linux

Cada [release](https://github.com/Ricky182771/moss/releases) trae tres formatos para Linux x86_64, con la interfaz gráfica (`moss-gui`), la de terminal (`moss`) y el bot de Telegram, más `SHA256SUMS` para revisarlos (`sha256sum -c SHA256SUMS --ignore-missing`):

- **Fedora** (`.rpm`):

  ```bash
  sudo dnf install ./moss-0.1.0-1.x86_64.rpm
  ```

- **Debian 12, Ubuntu 22.04 o más nuevos** (`.deb`):

  ```bash
  sudo apt install ./moss_0.1.0-1_amd64.deb
  ```

- **Cualquier lugar** (`.tar.gz`): se descomprime donde sea y se corre desde ahí; su `LEEME.md` dice qué instalar del sistema (GTK 3, libcurl, EGL y GLES).

  ```bash
  tar -xzf moss-0.1.0-linux-x86_64.tar.gz
  ./moss-0.1.0/moss-gui
  ```

Los paquetes instalan todo en `/usr/lib/moss/`, con `moss`, `moss-gui` y `moss-rpc` en `/usr/bin/`, y Moss aparece en el menú de aplicaciones. Desinstalar (`sudo dnf remove moss` o `sudo apt remove moss`) no borra tu configuración ni tus chats (`~/.config/moss` y `~/.local/share/moss`). Si el bot de Telegram está activo y cambias de formato o mueves la carpeta del `.tar.gz`, abre F2 → Addons → Telegram y guarda, para que el servicio apunte al `moss` nuevo.

Qué lleva cada paquete y por qué no traen modo portable: [17-empaquetado.md](17-empaquetado.md#qué-va-adentro).
