import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/screens/settings_screen.dart';
import 'package:moss_gui/strings.dart';

import 'harness.dart';

void main() {
  testWidgets('sin configurar: la tarjeta abre Configuración → Proveedor', (
    tester,
  ) async {
    const problem = 'Falta elegir el proveedor y el modelo.';
    await startApp(
      tester,
      handlers: {
        'app.status': (_) => appStatus(ready: false, problem: problem),
      },
    );
    expect(find.text(problem), findsOneWidget);
    await tester.tap(find.text(Strings.goToSettings));
    await tester.pumpAndSettle();
    expect(find.byType(SettingsScreen), findsOneWidget);
    expect(find.text(Strings.providerTitle), findsOneWidget);
  });

  testWidgets('configurado: sin tarjeta', (tester) async {
    await startApp(tester);
    expect(find.text(Strings.goToSettings), findsNothing);
  });

  testWidgets('Opciones: el enlace a Configuración del modelo y la nota de '
      'los sin key', (tester) async {
    await startApp(
      tester,
      handlers: {
        'settings.provider.get': (_) => providerSettings(),
        'commands.list': (_) => commandList(),
      },
    );
    await tester.tap(find.bySemanticsLabel(Strings.options));
    await tester.pumpAndSettle();
    await tester.tap(find.text('NVIDIA NIM'));
    await tester.pumpAndSettle();
    expect(find.text(Strings.addKeyIn('sin key')), findsOneWidget);

    await tester.tap(find.text(Strings.modelTitle));
    await tester.pumpAndSettle();
    expect(find.byType(SettingsScreen), findsOneWidget);
    expect(
      find.text(Strings.modelTitleFor('z-ai/glm5', 'NVIDIA NIM')),
      findsOneWidget,
    );
  });
}
