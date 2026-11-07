import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/rpc/client.dart';
import 'package:moss_gui/screens/chat_screen.dart';
import 'package:moss_gui/state/app_state.dart';
import 'package:moss_gui/theme/theme.dart';

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
  Key? key,
  Future<RpcClient> Function()? reconnect,
}) async {
  tester.view.physicalSize = size;
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
  final server = FakeServer(
    handlers: {
      'conversation.get': (_) => conversation ?? snapshot(),
      'conversations.list': (_) => {'items': summaries},
      'chat.send': (_) => {'accepted': true},
      'chat.cancel': (_) => {'cancelled': true},
      ...handlers,
    },
  );
  final client = server.client();
  await client.initialize();
  final app = AppState(client, reconnect: reconnect);
  await app.load('c1');
  await tester.pumpWidget(
    MediaQuery(
      data: MediaQueryData(size: size, disableAnimations: disableAnimations),
      child: RepaintBoundary(
        key: key,
        child: MaterialApp(
          theme: theme ?? MossTheme.dark(),
          debugShowCheckedModeBanner: false,
          home: ChatScreen(app),
        ),
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
