import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/screens/settings_screen.dart';
import 'package:moss_gui/strings.dart';

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
    expect(server.methods.where((m) => m.startsWith('settings.')), isEmpty);
  });
}
