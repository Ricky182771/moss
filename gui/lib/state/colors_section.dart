import '../models/gui_prefs.dart';
import 'settings_state.dart';

/// Colores y accesibilidad: el borrador es `app.prefs`, así lo que se elige
/// se ve al momento en toda la app; Cancelar regresa a [saved].
class ColorsSection extends SettingsSection {
  ColorsSection(super.app);

  /// Lo guardado en `settings.gui`.
  GuiPrefs saved = const GuiPrefs();

  GuiPrefs get draft => app.prefs.value;

  @override
  Set<String> get events => const {'gui'};

  @override
  bool get dirty => draft != saved;

  @override
  Future<void> load() async {
    final r = await rpc.request('settings.gui.get');
    saved = GuiPrefs.fromJson(r['value']);
    app.prefs.value = saved;
  }

  void set(GuiPrefs prefs) {
    app.prefs.value = prefs;
    notifyListeners();
  }

  @override
  void discard() {
    app.prefs.value = saved;
    notifyListeners();
  }

  @override
  List<SaveStep> get steps => [
    SaveStep(SaveOrder.gui, () async {
      final value = draft;
      await rpc.request('settings.gui.set', {'value': value.toJson()});
      saved = value;
      notifyListeners();
    }),
  ];
}
