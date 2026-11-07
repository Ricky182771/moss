import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/screens/settings_screen.dart';
import 'package:moss_gui/strings.dart';
import 'package:moss_gui/widgets/input_box.dart';

import 'harness.dart';

void main() {
  testWidgets('F2 abre Configuración y Esc regresa al chat', (tester) async {
    await startApp(tester);
    expect(find.byType(SettingsScreen), findsNothing);

    await tester.sendKeyEvent(LogicalKeyboardKey.f2);
    await tester.pumpAndSettle();
    expect(find.byType(SettingsScreen), findsOneWidget);
    expect(find.text(Strings.providerTitle), findsOneWidget);

    await tester.sendKeyEvent(LogicalKeyboardKey.escape);
    await tester.pumpAndSettle();
    expect(find.byType(SettingsScreen), findsNothing);
    expect(find.text(Strings.emptyPrompt), findsOneWidget);
  });

  testWidgets('al volver, el foco regresa a la caja del mensaje', (
    tester,
  ) async {
    await startApp(tester);
    final field = find.descendant(
      of: find.byType(InputBox),
      matching: find.byType(EditableText),
    );
    await tester.tap(field);
    await tester.pump();
    expect(Focus.of(tester.element(field)).hasFocus, isTrue);

    await tester.sendKeyEvent(LogicalKeyboardKey.f2);
    await tester.pumpAndSettle();
    expect(Focus.of(tester.element(field)).hasFocus, isFalse);

    await tester.sendKeyEvent(LogicalKeyboardKey.escape);
    await tester.pumpAndSettle();
    expect(Focus.of(tester.element(field)).hasFocus, isTrue);
  });

  testWidgets('la fila de la barra abre Configuración y Volver al chat sale', (
    tester,
  ) async {
    await startApp(tester);
    await tester.tap(find.text(Strings.settings));
    await tester.pumpAndSettle();
    expect(find.byType(SettingsScreen), findsOneWidget);

    await tester.tap(find.text(Strings.backToChat));
    await tester.pumpAndSettle();
    expect(find.byType(SettingsScreen), findsNothing);
  });

  testWidgets('↑ ↓ en el menú cambian de sección', (tester) async {
    await startApp(tester);
    await tester.sendKeyEvent(LogicalKeyboardKey.f2);
    await tester.pumpAndSettle();

    await tester.sendKeyEvent(LogicalKeyboardKey.arrowDown);
    await tester.pumpAndSettle();
    expect(find.text(Strings.colorsLead), findsOneWidget);

    await tester.sendKeyEvent(LogicalKeyboardKey.arrowUp);
    await tester.sendKeyEvent(LogicalKeyboardKey.arrowUp);
    await tester.pumpAndSettle();
    expect(find.text(Strings.searchLead), findsOneWidget);
  });

  testWidgets('Guardar está deshabilitado sin cambios', (tester) async {
    final (_, server) = await startApp(tester);
    await tester.sendKeyEvent(LogicalKeyboardKey.f2);
    await tester.pumpAndSettle();
    await tester.tap(find.text(Strings.save));
    await tester.pumpAndSettle();
    expect(
      server.methods.where((m) => m.endsWith('.set') || m.endsWith('.save')),
      isEmpty,
    );
  });
}
