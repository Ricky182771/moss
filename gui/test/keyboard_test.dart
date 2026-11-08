import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/screens/settings_screen.dart';
import 'package:moss_gui/widgets/input_box.dart';
import 'package:moss_gui/widgets/search_dialog.dart';
import 'package:moss_gui/widgets/settings_form.dart';

import 'fake_server.dart';
import 'harness.dart';

/// En Windows, AltGr llega como CtrlLeft + AltRight (`keyboard_manager.cc` del
/// motor). Una tecla no atendida sigue su camino y Windows manda el carácter.
Future<bool> _withAltGr(
  WidgetTester tester,
  LogicalKeyboardKey key, {
  String? character,
}) async {
  await tester.sendKeyDownEvent(LogicalKeyboardKey.controlLeft);
  await tester.sendKeyDownEvent(LogicalKeyboardKey.altRight);
  final handled = await tester.sendKeyDownEvent(key, character: character);
  await tester.sendKeyUpEvent(key);
  await tester.sendKeyUpEvent(LogicalKeyboardKey.altRight);
  await tester.sendKeyUpEvent(LogicalKeyboardKey.controlLeft);
  await tester.pump();
  return handled;
}

Future<void> _withCtrl(WidgetTester tester, LogicalKeyboardKey key) async {
  await tester.sendKeyDownEvent(LogicalKeyboardKey.controlLeft);
  await tester.sendKeyEvent(key);
  await tester.sendKeyUpEvent(LogicalKeyboardKey.controlLeft);
  await tester.pumpAndSettle();
}

/// Lo que en la distribución latinoamericana lleva AltGr, por tecla.
const _altGrKeys = {
  '@': LogicalKeyboardKey.keyQ,
  '{': LogicalKeyboardKey.quote,
  '}': LogicalKeyboardKey.backslash,
  '[': LogicalKeyboardKey.bracketLeft,
  ']': LogicalKeyboardKey.bracketRight,
  r'\': LogicalKeyboardKey.minus,
};

/// Sin AltGr: el acento muerto ´, ñ, ¿ y ¡. En Windows, la tecla lógica de estas
/// es el carácter, que el simulador no conoce: va la tecla de EE. UU. que ocupa
/// su lugar, y el carácter aparte.
const _plainKeys = {
  '´': LogicalKeyboardKey.bracketLeft,
  'ñ': LogicalKeyboardKey.semicolon,
  '¿': LogicalKeyboardKey.equal,
  '¡': LogicalKeyboardKey.minus,
};

/// Nada de esto se atiende: tiene que llegar al campo con el foco.
Future<void> _expectTyped(WidgetTester tester) async {
  for (final MapEntry(key: char, value: key) in _altGrKeys.entries) {
    expect(
      await _withAltGr(tester, key, character: char),
      isFalse,
      reason: 'AltGr: $char',
    );
  }
  for (final MapEntry(key: char, value: key) in _plainKeys.entries) {
    final handled = await tester.sendKeyDownEvent(key, character: char);
    await tester.sendKeyUpEvent(key);
    expect(handled, isFalse, reason: char);
  }
}

void main() {
  testWidgets('AltGr no dispara Ctrl+N, Ctrl+B ni Ctrl+K; Ctrl solo, sí', (
    tester,
  ) async {
    final (_, server) = await startApp(
      tester,
      handlers: {'conversation.new': (_) => snapshot()},
    );
    await tester.tap(find.byType(TextField));
    await tester.pump();
    final box = find.byType(InputBox);
    final left = tester.getTopLeft(box).dx;
    final calls = server.methods.length;

    for (final key in [
      LogicalKeyboardKey.keyN,
      LogicalKeyboardKey.keyB,
      LogicalKeyboardKey.keyK,
    ]) {
      expect(await _withAltGr(tester, key), isFalse, reason: '$key');
    }
    await tester.pumpAndSettle();
    expect(server.methods.length, calls);
    expect(tester.getTopLeft(box).dx, left);
    expect(find.byType(SearchDialog), findsNothing);

    // Las mismas teclas con Ctrl solo: la prueba distingue.
    await _withCtrl(tester, LogicalKeyboardKey.keyN);
    expect(server.methods, contains('conversation.new'));
    await _withCtrl(tester, LogicalKeyboardKey.keyB);
    expect(tester.getTopLeft(box).dx, isNot(left));
    await _withCtrl(tester, LogicalKeyboardKey.keyK);
    expect(find.byType(SearchDialog), findsOneWidget);
  });

  testWidgets('la caja deja pasar lo de AltGr, los acentos y ñ ¿ ¡', (
    tester,
  ) async {
    final (_, server) = await startApp(tester);
    await tester.tap(find.byType(TextField));
    await tester.pump();
    final calls = server.methods.length;
    await _expectTyped(tester);
    await tester.pumpAndSettle();
    expect(server.methods.length, calls);
  });

  testWidgets('los campos de Configuración también', (tester) async {
    await startApp(tester);
    await tester.sendKeyEvent(LogicalKeyboardKey.f2);
    await tester.pumpAndSettle();

    // El chat sigue montado debajo: solo los campos de Configuración.
    await tester.tap(
      find
          .descendant(
            of: find.byType(SettingsScreen),
            matching: find.byType(TextField),
          )
          .first,
    );
    await tester.pump();
    await _expectTyped(tester);

    // Una key: su bloqueo de Ctrl+C y Ctrl+X no se lleva a AltGr+C ni AltGr+X.
    await tester.tap(
      find
          .descendant(
            of: find.byType(SecretField),
            matching: find.byType(TextField),
          )
          .first,
    );
    await tester.pump();
    await _expectTyped(tester);
    expect(await _withAltGr(tester, LogicalKeyboardKey.keyC), isFalse);
    expect(await _withAltGr(tester, LogicalKeyboardKey.keyX), isFalse);
  });
}
