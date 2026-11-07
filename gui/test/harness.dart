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
  ],
  'saved': {'provider': provider, 'base_url': '', 'model': model},
  'effective': {'base_url': '', 'model': model},
  'locks': {'base_url': baseUrlLocked, 'model': modelLocked, 'api_key': false},
  'key_status': '',
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
