import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/main.dart';
import 'package:moss_gui/models/gui_prefs.dart';
import 'package:moss_gui/rpc/client.dart';
import 'package:moss_gui/screens/home_screen.dart';
import 'package:moss_gui/state/app_state.dart';

import 'fake_server.dart';

/// El reloj fijo de las pruebas (`AppState.now`): Hoy y Ayer en Ver registro
/// no dependen de la hora en que corren.
final kTestNow = DateTime(2026, 11, 7, 10);

/// Arranca la pantalla de chat contra un `moss-rpc` falso.
Future<(AppState, FakeServer)> startApp(
  WidgetTester tester, {
  Map<String, Object?>? conversation,
  List<Map<String, Object?>> summaries = const [],
  Map<String, Object? Function(Map<String, dynamic>)> handlers = const {},
  Size size = const Size(1440, 900),
  bool disableAnimations = false,
  ThemeData? theme,
  GuiPrefs? prefs,
  Key? key,
  Future<RpcClient> Function()? reconnect,
  List<String> addons = const [],
  DateTime? now,
}) async {
  // Por defecto, oscuro.
  final saved =
      prefs ??
      GuiPrefs(
        theme: theme?.brightness == Brightness.light
            ? ThemeChoice.light
            : ThemeChoice.dark,
      );
  tester.view.physicalSize = size;
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
  final server = FakeServer(
    handlers: {
      'conversation.get': (_) => conversation ?? snapshot(),
      'conversations.list': (_) => {'items': summaries},
      'chat.send': (_) => {'accepted': true},
      'chat.cancel': (_) => {'cancelled': true},
      'app.status': (_) => appStatus(),
      'settings.gui.get': (_) => {'value': saved.toJson()},
      'settings.provider.get': (_) => providerSettings(),
      'settings.provider.check': (_) => providerCheck(),
      'settings.search.get': (_) => {
        'configured': true,
        'locked': false,
        'key_status': 'guardada: …9d1e',
      },
      'settings.system_prompt.get': (_) => systemPrompt(),
      'settings.system_prompt.check': (p) => {
        'error': null,
        'warning': null,
        'bytes': (p['text'] as String).length,
      },
      'settings.search.check': (_) => {'error': null},
      'settings.model_params.get': (_) => modelParams(),
      'settings.model_params.check': (_) => {
        'field_errors': <String, String>{},
        'extra_error': null,
        'preview': '{\n  "model": "z-ai/glm5"\n}',
      },
      'models.list': (_) => {
        'models': ['z-ai/glm5', 'meta/llama-3.3-70b'],
      },
      ...handlers,
    },
  );
  final client = server.client();
  await client.initialize();
  // Como `Boot`: las preferencias guardadas antes del chat.
  final app = AppState(
    client,
    reconnect: reconnect,
    prefs: ValueNotifier(saved),
    addons: addons,
    now: () => now ?? kTestNow,
  );
  await app.load('c1');
  await tester.pumpWidget(
    MediaQuery(
      data: MediaQueryData(size: size, disableAnimations: disableAnimations),
      child: RepaintBoundary(
        key: key,
        child: PrefsApp(prefs: app.prefs, home: HomeScreen(app)),
      ),
    ),
  );
  await tester.pump();
  return (app, server);
}

/// Una respuesta larga, para que la lista se desplace.
Map<String, Object?> longChat({
  String phase = 'idle',
  bool inProgress = false,
}) {
  final long = List.filled(40, 'Una línea de la respuesta.').join('\n\n');
  return snapshot(
    rev: 2,
    phase: phase,
    entries: [
      entry('user', text: 'Explícame algo largo.'),
      entry('assistant', text: long, inProgress: inProgress),
    ],
  );
}

Map<String, Object?> appStatus({
  bool ready = true,
  String? problem,
  bool hasSearchKey = true,
}) => {
  'ready': ready,
  'problem': problem,
  'busy': false,
  'conversation': 'c1',
  'env_locks': <String>[],
  'has_search_key': hasSearchKey,
};

/// `settings.provider.get` con NVIDIA NIM (con key), Gemini (con key) y un
/// endpoint sin key.
Map<String, Object?> providerSettings({
  String provider = 'nvidia',
  String model = 'z-ai/glm5',
  bool baseUrlLocked = false,
  bool modelLocked = false,
  bool apiKeyLocked = false,
  String keyStatus = '',
  bool withLab = false,
}) => {
  'providers': [
    {
      'id': 'nvidia',
      'kind': 'builtin',
      'name': 'NVIDIA NIM',
      'base_url': 'https://integrate.api.nvidia.com/v1',
      'needs_key': true,
      'key_saved': true,
      'key_status': 'guardada: …abcd',
      'selectable': true,
      'note': '',
    },
    {
      'id': 'gemini',
      'kind': 'builtin',
      'name': 'Google Gemini',
      'base_url': 'https://generativelanguage.googleapis.com/v1beta/openai',
      'needs_key': true,
      'key_saved': true,
      'key_status': 'guardada: …wxyz',
      'selectable': true,
      'note': '',
    },
    {
      'id': 'custom:omniroute',
      'kind': 'endpoint',
      'name': 'OmniRoute',
      'base_url': 'https://mi-gateway.example/v1',
      'needs_key': true,
      'key_saved': false,
      'key_status': null,
      'selectable': false,
      'note': 'sin key',
    },
    if (withLab)
      {
        'id': 'custom:lab',
        'kind': 'endpoint',
        'name': 'Servidor del lab',
        'base_url': 'http://127.0.0.1:8080/v1',
        'needs_key': false,
        'key_saved': false,
        'key_status': null,
        'selectable': true,
        'note': '',
      },
  ],
  'saved': {
    'provider': provider,
    'base_url': switch (provider) {
      'custom:omniroute' => 'https://mi-gateway.example/v1',
      'custom:lab' => 'http://127.0.0.1:8080/v1',
      _ => '',
    },
    'model': model,
  },
  'effective': {'base_url': '', 'model': model},
  'locks': {
    'base_url': baseUrlLocked,
    'model': modelLocked,
    'api_key': apiKeyLocked,
  },
  'key_status': keyStatus,
};

const kDefaultPrompt =
    'Eres un asistente útil. Responde en español, de forma clara y concisa.';

Map<String, Object?> systemPrompt({String mode = 'default', String? text}) => {
  'mode': mode,
  'text': text ?? (mode == 'default' ? kDefaultPrompt : ''),
  'default_text': kDefaultPrompt,
  'max_bytes': 8000,
  'warning': null,
};

/// `settings.model_params.get` de NIM con temperatura guardada.
Map<String, Object?> modelParams({
  Map<String, Object?> params = const {'temperature': 0.6},
  String extra = '',
}) => {
  'provider': 'nvidia',
  'model': 'z-ai/glm5',
  'params': {
    for (final k in [
      'reasoning',
      'temperature',
      'top_p',
      'top_k',
      'max_tokens',
      'frequency_penalty',
      'presence_penalty',
      'seed',
    ])
      k: params[k],
  },
  'extra': extra,
  'reasoning_style': 'toggle',
  'limits': {
    'temperature': {
      'min': 0.0,
      'max': 2.0,
      'min_exclusive': false,
      'integer': false,
    },
    'top_p': {'min': 0.0, 'max': 1.0, 'min_exclusive': true, 'integer': false},
    'top_k': {'min': 1, 'max': 1000, 'min_exclusive': false, 'integer': true},
    'max_tokens': {
      'min': 1,
      'max': 1000000,
      'min_exclusive': false,
      'integer': true,
    },
  },
  'help': const {
    'reasoning': kReasoningHelp,
    'top_k': kTopKHelp,
    'extra': kExtraHelp,
  },
  'preview': '{\n  "model": "z-ai/glm5",\n  "temperature": 0.6\n}',
};

/// Lo que manda el servidor en `help`: la GUI no tiene estos textos.
const kReasoningHelp =
    'En NIM, Bajo, Medio y Alto solo lo encienden (enable_thinking); Apagado '
    'lo apaga.';
const kTopKHelp = 'No todos los proveedores lo aceptan.';
const kExtraHelp =
    'Se mezcla al final del cuerpo (JSON Merge Patch): gana sobre el '
    'formulario y null quita una llave.';

Map<String, Object?> providerCheck({
  String? error,
  String? errorField,
  String? hint,
  bool canRequestModels = true,
}) => {
  'error': error,
  'error_field': errorField,
  'hint': hint,
  'can_request_models': canRequestModels,
  'dirty': false,
};

Map<String, Object?> commandList({
  String searchNote = '',
  String modelNote = '',
}) => {
  'commands': [
    {
      'name': '/buscar',
      'arguments': '<consulta>',
      'description': 'Busca en la web y responde con fuentes citadas',
      'needs_argument': true,
      'available': searchNote.isEmpty,
      'note': searchNote,
    },
    {
      'name': '/model',
      'arguments': '[proveedor] [modelo]',
      'description': 'Cambia de modelo',
      'needs_argument': false,
      'available': modelNote.isEmpty,
      'note': modelNote,
    },
  ],
};

/// Las etiquetas de `addons.telegram.get` (las de `addon_field_label`).
const kTelegramLabels = {
  'token': 'Token',
  'model': 'Modelo',
  'model_key': 'Key del modelo',
  'search_key': 'Key de búsqueda',
  'allowed_users': 'IDs autorizados',
  'invite_code': 'Código de invitación',
  'daily_limit': 'Mensajes por día',
  'concurrent': 'Respuestas a la vez',
  'idle_minutes': 'Min. de inactividad',
  'stale_minutes': 'Min. mensaje viejo',
  'draft_ms': 'Vista previa (ms)',
  'time_zone': 'Zona horaria',
};

Map<String, Object?> telegramGet({
  bool enabled = true,
  bool logErrorDetails = false,
  Map<String, String> fields = const {},
  bool readOnly = false,
  String? readOnlyReason,
  String? loadError,
  String envNote = '',
  List<String> discarded = const [],
}) => {
  'form': {
    'enabled': enabled,
    'log_error_details': logErrorDetails,
    'fields': {
      'token': '',
      'model': '',
      'model_key': '',
      'search_key': '',
      'allowed_users': '123456789, 987654321',
      'invite_code': '',
      'daily_limit': '34',
      'concurrent': '4',
      'idle_minutes': '60',
      'stale_minutes': '10',
      'draft_ms': '1000',
      'time_zone': 'America/Mexico_City',
      ...fields,
    },
    'remove_model_key': false,
    'remove_search_key': false,
  },
  'secrets': {
    'token': 'guardada: …x9Qe',
    'model_key': 'sin configurar',
    'search_key': 'sin configurar',
  },
  'labels': kTelegramLabels,
  'env_note': envNote,
  'read_only': readOnly,
  'read_only_reason': readOnlyReason,
  'discarded': discarded,
  'load_error': loadError,
};

Map<String, Object?> serviceStatus({
  bool systemd = true,
  String active = 'active',
  String summary = 'Servicio: activo · habilitado',
  String problem = '',
  String mismatch = '',
  String? lingerNotice,
  String? lingerCommand,
}) => {
  'systemd': systemd,
  'active': active,
  'enabled': 'enabled',
  'exit_status': null,
  'unit': 'current',
  'linger': lingerNotice == null,
  'linger_notice': lingerNotice,
  'linger_command': lingerCommand,
  'last_error': '',
  'summary': summary,
  'problem': problem,
  'mismatch': mismatch,
  'journal_command': 'journalctl --user -u moss-telegram -f',
};

Map<String, Object?> addonInstall({
  bool installed = true,
  String problem = '',
}) => {
  'name': 'telegram',
  'installed': installed,
  'path': installed ? '/usr/local/bin/moss-telegram' : null,
  'problem': problem,
  'config_version_ok': installed && problem.isEmpty ? true : null,
};

/// Los manejadores de la sección Telegram, para `startApp(handlers:)`.
Map<String, Object? Function(Map<String, dynamic>)> telegramHandlers({
  Map<String, Object?>? get,
  Map<String, Object?>? status,
  List<String> invited = const [],
  bool needsRestart = false,
}) => {
  'addons.telegram.get': (_) => get ?? telegramGet(),
  'addons.telegram.invited.list': (_) => {'users': invited},
  'addons.list': (_) => {
    'addons': [addonInstall()],
  },
  'addons.check_install': (_) => addonInstall(),
  'service.status': (_) => status ?? serviceStatus(),
  'addons.telegram.check': (_) => {
    'field_errors': <String, String>{},
    'form_error': null,
    'needs_restart': needsRestart,
  },
  'addons.telegram.save': (_) => {'needs_restart': needsRestart},
  'service.apply': (_) => {
    'message': 'Configuración aplicada al bot.',
    'ok': true,
  },
};

String _two(int n) => n.toString().padLeft(2, '0');

/// Una línea `short-iso` del día [day] (relativo a [kTestNow]: 0, -1…).
String journalLine(int day, String time, String message) {
  final d = DateTime(kTestNow.year, kTestNow.month, kTestNow.day + day);
  return '${d.year}-${_two(d.month)}-${_two(d.day)}T$time-0600 moss-prueba '
      'moss-telegram[4242]: $message';
}

/// 30 líneas de ayer y 30 de hoy.
List<String> manyLines() => [
  for (var i = 0; i < 30; i++)
    journalLine(-1, '22:${_two(i)}:00', 'mensaje de ayer $i'),
  for (var i = 0; i < 30; i++)
    journalLine(0, '09:${_two(i)}:00', 'mensaje de hoy $i'),
];

Map<String, Object?> logResult({
  List<String>? lines,
  List<int> errors = const [],
  bool truncated = false,
  String notice = '',
}) => {
  'lines': lines ?? manyLines(),
  'errors': errors,
  'truncated': truncated,
  'notice': notice,
};
