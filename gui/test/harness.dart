import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/main.dart';
import 'package:moss_gui/models/gui_prefs.dart';
import 'package:moss_gui/rpc/client.dart';
import 'package:moss_gui/screens/home_screen.dart';
import 'package:moss_gui/state/app_state.dart';

import 'fake_server.dart';

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
        'selectable': true,
        'note': '',
      },
  ],
  'saved': {'provider': provider, 'base_url': '', 'model': model},
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
  'preview': '{\n  "model": "z-ai/glm5",\n  "temperature": 0.6\n}',
};

Map<String, Object?> providerCheck({
  String? error,
  String? hint,
  bool canRequestModels = true,
}) => {
  'error': error,
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
