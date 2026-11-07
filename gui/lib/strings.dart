/// Todos los textos propios de la GUI. Los que vienen de `moss-rpc` se
/// muestran tal cual.
class Strings {
  static const appName = 'moss';

  // Barra lateral
  static const newChat = 'Nuevo chat';
  static const newChatKey = 'Ctrl+N';
  static const searchChats = 'Buscar chats';
  static const searchChatsKey = 'Ctrl+K';
  static const searchChatsHint = 'Buscar por título';
  static const noChatsFound = 'Ningún chat coincide';
  static const chats = 'Chats';
  static const withWebSearch = 'Con búsqueda web';
  static String moreOptions(String title) => 'Más opciones de «$title»';
  static const chatMenu = 'Opciones del chat';
  static const rename = 'Renombrar';
  static const chatTitle = 'Título del chat';
  static const delete = 'Borrar…';
  static String deleteQuestion(String title) => '¿Borrar «$title»?';
  static const cannotUndo = 'No se puede deshacer.';
  static const cancel = 'Cancelar';
  static const yes = 'Sí';
  static const no = 'No';
  static const confirmDelete = 'Borrar';

  // Encabezado
  static const hideSidebar = 'Ocultar barra lateral (Ctrl+B)';
  static const showSidebar = 'Mostrar barra lateral (Ctrl+B)';
  static const export = 'Exportar';

  // Chat
  static const emptyPrompt = '¿En qué te ayudo?';
  static const goToSettings = 'Ir a Configuración';
  static const searching = 'Buscando en la web…';
  static const thinking = 'Pensando…';
  static String thoughtSummary(String count, bool one) =>
      'Pensamiento · $count ${one ? 'palabra' : 'palabras'}';
  static const cancelled = '(cancelada)';
  static const incomplete = '(respuesta incompleta)';
  static const sources = 'Fuentes';
  static const copy = 'Copiar';
  static const copied = 'Copiado ✓';

  // Caja del mensaje
  static const message = 'Mensaje';
  static const messageHint = 'Escribe tu mensaje';
  static const send = 'Enviar';
  static const stop = 'Detener';
  static const help = 'Enter envía · Shift+Enter nueva línea · / comandos';

  // Menú de /
  static const commands = 'Comandos';
  static const noCommands =
      'Sin comandos que coincidan · se enviará como mensaje';

  // Opciones
  static const options = 'Opciones';
  static const modelSection = 'Modelo';
  static const providers = 'Proveedores';
  static String modelsOf(String provider) => 'Modelos de $provider';
  static const filterModels = 'Filtrar modelos';
  static const filterProviders = 'Filtrar proveedores';
  static const loadingModels = 'Cargando modelos…';
  static const noMatches = 'Sin coincidencias';
  static const typeModel = 'Escribe el modelo y presiona Enter';
  static String lockedBy(String variable) => 'Fijado por $variable';
  static String addKeyIn(String note) => '$note · agrégala en Configuración';
  static const searchWeb = 'Buscar en la web';
  static const searchWebHint = 'Responde con fuentes citadas';
  static const noSearchKey = 'Falta la key de búsqueda';
  static const provider = 'Proveedor';
  static const webSearch = 'Búsqueda web';
  static const removeWebSearch = 'Quitar la búsqueda web';

  // Configuración
  static const settings = 'Configuración';
  static const settingsKey = 'F2';
  static const backToChat = 'Volver al chat';
  static const escKey = 'Esc';
  static const settingsSections = 'Secciones de configuración';
  static String withUnsaved(String section) =>
      '$section, con cambios sin guardar';
  static const save = 'Guardar';
  static const saving = 'Guardando…';
  static const discardQuestion = '¿Descartar los cambios?';
  static const discard = 'Descartar';
  static const keepEditing = 'Seguir editando';
  static const providerMenu = 'Proveedor';
  static const providerTitle = 'Proveedor de IA';
  static const providerLead =
      'A qué servidor se conecta Moss y con qué modelo platica.';
  static const colorsTitle = 'Colores y accesibilidad';
  static const colorsLead =
      'Cómo se ve Moss. Los cambios se ven al momento y se quedan al guardar.';
  static const modelTitle = 'Configuración del modelo';
  static const modelLead =
      'Las instrucciones valen para todos los modelos; los ajustes, solo para '
      'el modelo en uso.';
  static const searchLead =
      'Para que el modelo pueda consultar internet antes de responder.';
  static const theme = 'Tema';
  static const themeSystem = 'Sistema';
  static const themeSystemNote = 'Sigue a Windows o Linux';
  static const themeDark = 'Moss oscuro';
  static const themeDarkNote = 'Fondo casi negro';
  static const themeLight = 'Moss claro';
  static const themeLightNote = 'Fondo hueso';
  static const reading = 'Lectura';
  static const textSize = 'Tamaño del texto';
  static const textSmall = 'Chico';
  static const textNormal = 'Normal';
  static const textLarge = 'Grande';
  static const reduceMotion = 'Reducir animaciones';
  static const reduceMotionNote =
      'Quita el desvanecido de los mensajes y deja fijo el cursor de la '
      'respuesta.';
  static const highContrast = 'Más contraste';
  static const highContrastNote = 'Bordes y textos tenues más marcados.';
  static const providersNote =
      'Los que no tienen key se ven, pero piden una para usarse.';
  static const fromTable = 'De la lista';
  static const yourEndpoints = 'Tus endpoints';
  static const newEndpoint = 'Endpoint nuevo';
  static const addEndpoint = 'Agregar endpoint…';
  static const inUse = 'en uso';
  static const local = 'local';
  static const keySaved = 'key guardada';
  static const endpointName = 'Nombre';
  static const baseUrl = 'URL base';
  static const baseUrlHelp =
      'La dirección del servidor compatible con OpenAI, normalmente termina '
      'en /v1.';
  static const apiKey = 'API key';
  static const deleteEndpoint = 'Borrar endpoint';
  static const endpointInUse =
      'Es el que está en uso: cambia de proveedor antes de borrarlo.';
  static String deleteEndpointQuestion(String name) =>
      '¿Borrar el endpoint $name?';
  static const refresh = 'Actualizar';
  static String modelsCount(int n) =>
      '$n ${n == 1 ? 'modelo' : 'modelos'} del servidor. Si la lista no '
      'carga, escribe el nombre del modelo a mano.';
  static const modelByHand = 'Modelo';
  static const typeModelName = 'Escribe el nombre del modelo';
  static const searchState = 'Estado';
  static const searchConfigured = 'Configurada';
  static const searchNotConfigured = 'Sin configurar';
  static const searchKeyTitle = 'API key de búsqueda';
  static const searchKeyNote =
      'Se usa con el botón Buscar en la web y con el comando /buscar.';
  static const newApiKey = 'Nueva API key';
  static const searchEnvHelp =
      'Si la variable CHAT_SEARCH_API_KEY está definida, se usa esa y aquí no '
      'se puede cambiar.';
  static String modelTitleFor(String model, String provider) =>
      'Configuración del modelo — $model ($provider)';
  static const systemPrompt = 'Instrucciones del sistema';
  static const forAllModels = 'Para todos los modelos';
  static const promptMode = 'Modo de las instrucciones';
  static const promptDefault = 'Predeterminadas';
  static const promptNone = 'Ninguna';
  static const promptCustom = 'Personalizadas';
  static const promptText = 'Instrucciones';
  static String bytesCounter(String bytes, String max) => '$bytes / $max bytes';
  static const restorePrompt = 'Restaurar predeterminadas';
  static const modelParams = 'Ajustes del modelo';
  static const goToProvider = 'Ir a Proveedor';
  static String paramsOf(String model) => 'Ajustes de $model';
  static String paramsNote(String provider) =>
      '$provider · Vacío = predeterminado del modelo: no se manda.';
  static const restoreAll = 'Restaurar todo';
  static const reasoning = 'Razonamiento';
  static const reasoningDefault = 'Predeterminado';
  static const reasoningOff = 'Apagado';
  static const reasoningLow = 'Bajo';
  static const reasoningMedium = 'Medio';
  static const reasoningHigh = 'Alto';

  /// shortcut: copia los textos de la TUI (`cli/src/model_params_form.cpp`,
  /// `reasoning_help`) por `reasoning_style`; en `effort` va el genérico, sin
  /// el caso de OpenRouter (`reasoning.effort`). Lo correcto es que
  /// `settings.model_params.get` mande el texto, en un PR de protocolo.
  static String? reasoningHelp(String style) => switch (style) {
    'toggle' =>
      'En NIM, Bajo, Medio y Alto solo lo encienden (enable_thinking); '
          'Apagado lo apaga.',
    'budget' =>
      'Bajo, Medio y Alto dan 1024, 4096 y 16384 tokens para pensar; '
          'Apagado no manda nada.',
    'effort' =>
      'Manda reasoning_effort (Apagado: "none"). No todos los modelos lo '
          'aceptan.',
    _ => null,
  };
  // shortcut: estas dos notas repiten reglas del núcleo (qué llaves pone
  // Moss) y de los proveedores; deberían venir de `model_params.get`.
  static const topKNote =
      'Top K no es del estándar de OpenAI: no todos los proveedores lo '
      'aceptan.';
  static String paramLabel(String key) => switch (key) {
    'temperature' => 'Temperatura',
    'top_p' => 'Top P',
    'top_k' => 'Top K',
    'max_tokens' => 'Máx. tokens de salida',
    'frequency_penalty' => 'Penalización por frecuencia',
    'presence_penalty' => 'Penalización por presencia',
    'seed' => 'Semilla',
    _ => key,
  };
  static const paramDefault = 'Predeterminado';
  static String range(String min, String max, {required bool exclusive}) =>
      exclusive ? 'más de $min, hasta $max' : '$min a $max';
  static const advanced = 'Avanzado';
  static const advancedNote = 'JSON que se mezcla al final';
  static const extraJson = 'JSON avanzado';
  static const extraHelp =
      'Gana sobre el formulario; null quita una llave. No se pueden cambiar '
      'model, messages ni stream.';
  static const preview = 'Vista previa de lo que se envía';
  static const previewLabel = 'Vista previa del cuerpo de la petición';
  static const modelsFailed = 'No se pudieron pedir los modelos.';
  static const envBaseUrl = 'CHAT_BASE_URL';
  static const envModel = 'CHAT_MODEL';
  static const envApiKey = 'CHAT_API_KEY';
  static const envSearchKey = 'CHAT_SEARCH_API_KEY';
  static const notANumber = 'Escribe un número.';
  static const notAnInteger = 'Escribe un número entero.';
  static const showKey = 'Ver lo que escribes';
  static const hideKey = 'Ocultar lo que escribes';
  static const newKeyHint = 'Escribe una API key nueva';
  static const keepKeyHelp =
      'Déjala vacía para conservar la guardada. Nunca se muestra completa.';
  static const reviewMarked = 'Revisa los campos marcados: no se guardó nada.';
  static const changedOutside =
      'Esto se cambió fuera de aquí mientras lo editabas. Cancelar trae lo '
      'guardado; Guardar lo reemplaza con lo tuyo.';

  // Configuración → Telegram (maqueta Config-addons; preguntas y avisos de la
  // F2 de la TUI, `addons_panel.cpp`)
  static const telegramMenu = 'Addons · Telegram';
  static const telegramTitle = 'Telegram';
  static const telegramLead =
      'Un bot para que los alumnos platiquen con Moss desde Telegram.';
  static const telegramLinuxOnly = 'El bot de Telegram funciona en Linux.';
  static const telegramBarHint =
      'Guardar no reinicia el servicio: Moss te pregunta.';
  static String serviceFoot(String summary) => 'Telegram · $summary';
  static const serviceSection = 'Servicio';
  static const installedLabel = 'Instalado';
  static const serviceLabel = 'Servicio';
  static const restartService = 'Reiniciar';
  static const refreshStatus = 'Actualizar estado';
  static const viewLog = 'Ver registro';
  static const botSection = 'Bot';
  static const enabledTitle = 'Activado';
  static const enabledNote = 'El servicio arranca solo al iniciar sesión.';
  static const tokenHint = 'Escribe un token nuevo';
  static const keepTokenHelp =
      'Déjalo vacío para conservar el guardado. Nunca se muestra completo.';
  static const optionalHint = 'Opcional';
  static const removeKey = 'Quitar';
  static const keepSavedKey = 'No quitar';
  static const keyWillBeRemoved = 'se quita: se usará la de la app';
  static const sameModelAsApp = 'Mismo que la app';
  static const ownKeyHelp =
      'Sin key propia se usan las de la app. Recomendado: una propia con '
      'tope diario en el proveedor.';
  static const accessSection = 'Acceso';
  static const allowedUsersHelp = 'Separados por comas.';
  static const generateCode = 'Generar código';
  static const inviteCodeHelp =
      'Quien lo mande al bot entra a la lista de invitados.';
  static const limitsSection = 'Límites';
  static const logDetailsTitle = 'Detalle de errores en el registro';
  static const logDetailsNote =
      'Puede incluir texto de los alumnos: solo para diagnosticar.';
  static String invitedTitle(int n) => 'Invitados ($n)';
  static const noInvited = 'Nadie ha entrado con el código';
  static const noInvitedNote =
      'Cuando alguien lo use, aparece aquí y lo puedes quitar.';
  static const removeInvited = 'Quitar';
  static String removeInvitedLabel(String user) => 'Quitar a $user';
  static String removeInvitedQuestion(String user) => '¿Quitar $user ahora?';
  static const removeInvitedDetail = 'No espera a Guardar.';
  static const restartQuestion =
      'Este cambio necesita reiniciar el bot y se borran las conversaciones '
      'en memoria. ¿Reiniciar ahora?';
  static const cancelledNothingSaved = 'Cancelado: no se guardó nada.';
  static const savedServiceUntouched =
      'Cancelado: se guardó, pero no se tocó el servicio.';
  static const savedForeignKept = 'Guardado. No se tocó el servicio existente.';
  static const savedForeignUntouched =
      'Cancelado: se guardó, pero no se tocó el servicio existente.';
  static String savedWith(String message) =>
      message.isEmpty ? 'Guardado.' : 'Guardado. $message';

  // moss-rpc
  static const rpcNotFound = 'No se encontró moss-rpc';
  static const rpcCannotStart = 'moss-rpc no arrancó';
  static const rpcNotResponding = 'moss-rpc no respondió';
  static const rpcOtherVersion = 'moss-rpc es de otra versión de Moss';
  static const rpcOtherVersionDetail =
      'Instala la app y moss-rpc de la misma versión.';
  static const rpcSearchedAt = 'Se buscó en';
  static const rpcStderr = 'Últimas líneas de moss-rpc';
  static const rpcDisconnected = 'moss-rpc se cerró.';
  static const retry = 'Reintentar';
}
