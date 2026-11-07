import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../state/app_state.dart';
import '../state/colors_section.dart';
import '../state/provider_section.dart';
import '../state/search_section.dart';
import '../state/settings_state.dart';
import '../strings.dart';
import '../theme/colors.dart';
import '../theme/motion.dart';
import '../theme/theme.dart';
import '../widgets/colors_panel.dart';
import '../widgets/controls.dart';
import '../widgets/delete_dialog.dart';
import '../widgets/logo.dart';
import '../widgets/provider_panel.dart';
import '../widgets/search_panel.dart';
import '../widgets/sidebar.dart' show kSidebarWidth;
import '../widgets/toast.dart';

/// Lo que cada sección pone en la pantalla: su título, el texto de abajo y
/// su panel.
class SettingsPanel {
  final String menu;
  final String title;
  final String lead;
  final WidgetBuilder? body;
  const SettingsPanel({
    required this.menu,
    required this.title,
    required this.lead,
    this.body,
  });
}

/// Las secciones y sus paneles. Para agregar una: un valor en [SettingsPage],
/// su [SettingsSection] aquí y su [SettingsPanel] en [_panel].
Map<SettingsPage, SettingsSection> _sections(AppState app) => {
  SettingsPage.provider: ProviderSection(app),
  SettingsPage.colors: ColorsSection(app),
  SettingsPage.search: SearchSection(app),
};

SettingsPanel _panel(SettingsPage page, SettingsSection? section) =>
    switch (page) {
      SettingsPage.provider => SettingsPanel(
        menu: Strings.providerMenu,
        title: Strings.providerTitle,
        lead: Strings.providerLead,
        body: (_) => ProviderPanel(section! as ProviderSection),
      ),
      SettingsPage.colors => SettingsPanel(
        menu: Strings.colorsTitle,
        title: Strings.colorsTitle,
        lead: Strings.colorsLead,
        body: (_) => ColorsPanel(section! as ColorsSection),
      ),
      SettingsPage.model => const SettingsPanel(
        menu: Strings.modelTitle,
        title: Strings.modelTitle,
        lead: Strings.modelLead,
      ),
      SettingsPage.search => SettingsPanel(
        menu: Strings.webSearch,
        title: Strings.webSearch,
        lead: Strings.searchLead,
        body: (_) => SearchPanel(section! as SearchSection),
      ),
    };

/// DISENO.md → Pantallas → Configuración: menú de secciones a la izquierda,
/// el panel que se desplaza solo y la barra Cancelar / Guardar fija abajo.
class SettingsScreen extends StatefulWidget {
  final AppState app;
  final SettingsPage page;
  const SettingsScreen(this.app, {super.key, required this.page});

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  late final SettingsState _state = SettingsState(
    widget.app,
    _sections(widget.app),
    page: widget.page,
  );
  final _menu = {
    for (final p in SettingsPage.values) p: FocusNode(debugLabel: p.name),
  };

  @override
  void didUpdateWidget(SettingsScreen old) {
    super.didUpdateWidget(old);
    if (old.page != widget.page) _state.show(widget.page);
  }

  @override
  void dispose() {
    _state.dispose();
    for (final f in _menu.values) {
      f.dispose();
    }
    super.dispose();
  }

  /// Volver al chat (y Esc): con cambios, "¿Descartar los cambios?".
  Future<void> _close() async {
    if (_state.saving) return;
    if (_state.dirty) {
      final yes = await confirm(
        context,
        question: Strings.discardQuestion,
        cancelLabel: Strings.keepEditing,
        confirmLabel: Strings.discard,
        action: () async => null,
      );
      if (!yes || !mounted) return;
      // Ahora y no al desmontarse: la vista previa de Colores regresa a lo
      // guardado mientras se desvanece.
      _state.discard();
    }
    widget.app.settingsOpen.value = null;
  }

  /// ↑ ↓ en el menú: cambian de sección y llevan el foco.
  KeyEventResult _menuKey(FocusNode _, KeyEvent e) {
    if (e is! KeyDownEvent && e is! KeyRepeatEvent) {
      return KeyEventResult.ignored;
    }
    final step = switch (e.logicalKey) {
      LogicalKeyboardKey.arrowDown => 1,
      LogicalKeyboardKey.arrowUp => -1,
      _ => 0,
    };
    if (step == 0) return KeyEventResult.ignored;
    const pages = SettingsPage.values;
    final next = pages[(pages.indexOf(_state.page) + step) % pages.length];
    _state.show(next);
    _menu[next]!.requestFocus();
    return KeyEventResult.handled;
  }

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    return CallbackShortcuts(
      bindings: {const SingleActivator(LogicalKeyboardKey.escape): _close},
      child: FocusScope(
        autofocus: true,
        child: Material(
          color: c.background,
          child: ListenableBuilder(
            listenable: _state,
            builder: (context, _) => Row(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                FocusTraversalGroup(child: _menuColumn(context)),
                Expanded(
                  child: FocusTraversalGroup(
                    child: Column(
                      children: [
                        Expanded(
                          child: Stack(
                            children: [
                              Positioned.fill(child: _panelView(context)),
                              Positioned(
                                left: 24,
                                right: 24,
                                bottom: 12,
                                child: Center(child: ToastHost(_state.toast)),
                              ),
                            ],
                          ),
                        ),
                        _bar(context),
                      ],
                    ),
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }

  Widget _menuColumn(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    return Container(
      width: kSidebarWidth,
      decoration: BoxDecoration(
        color: c.sidebar,
        border: Border(right: BorderSide(color: c.border)),
      ),
      padding: const EdgeInsets.fromLTRB(14, 20, 14, 20),
      child: SingleChildScrollView(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          spacing: 20,
          children: [
            Pressable(
              onPressed: _close,
              minHeight: 44,
              padding: const EdgeInsets.symmetric(horizontal: 10),
              child: Row(
                spacing: 10,
                children: [
                  Icon(Icons.chevron_left_rounded, size: 18, color: c.text),
                  Expanded(child: Text(Strings.backToChat, style: s.row)),
                  Text(Strings.escKey, style: s.kbd),
                ],
              ),
            ),
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 10),
              child: Row(
                spacing: 10,
                children: [
                  const MossLogo(size: 22),
                  Flexible(
                    child: Semantics(
                      header: true,
                      child: Text(Strings.settings, style: s.brand),
                    ),
                  ),
                ],
              ),
            ),
            Semantics(
              label: Strings.settingsSections,
              container: true,
              explicitChildNodes: true,
              child: Focus(
                skipTraversal: true,
                onKeyEvent: _menuKey,
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  spacing: 2,
                  children: [
                    for (final p in SettingsPage.values) _menuItem(context, p),
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _menuItem(BuildContext context, SettingsPage p) {
    final c = context.colors;
    final s = context.styles;
    final current = p == _state.page;
    final dirty = _state.sections[p]?.dirty ?? false;
    final name = _panel(p, null).menu;
    return Semantics(
      label: dirty ? Strings.withUnsaved(name) : name,
      excludeSemantics: true,
      selected: current,
      button: true,
      child: Pressable(
        focusNode: _menu[p],
        autofocus: current,
        selected: current,
        onPressed: () => _state.show(p),
        color: current ? c.accentSoft : null,
        minHeight: 44,
        padding: const EdgeInsets.symmetric(horizontal: 12),
        child: Row(
          spacing: 10,
          children: [
            Expanded(child: Text(name, style: current ? s.rowOpen : s.row)),
            // La marca de cambios sin guardar.
            if (dirty)
              Container(
                width: 8,
                height: 8,
                decoration: BoxDecoration(
                  color: c.accent,
                  shape: BoxShape.circle,
                ),
              ),
          ],
        ),
      ),
    );
  }

  Widget _panelView(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final page = _state.page;
    final section = _state.sections[page];
    final panel = _panel(page, section);
    return Scrollbar(
      key: ValueKey(page),
      child: SingleChildScrollView(
        padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 40),
        child: Center(
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 760),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              spacing: 24,
              children: [
                Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  spacing: 6,
                  children: [
                    Semantics(
                      header: true,
                      child: Text(panel.title, style: s.pageTitle),
                    ),
                    Text(panel.lead, style: s.lead),
                  ],
                ),
                if (section != null && section.changedOutside)
                  Semantics(
                    liveRegion: true,
                    child: Row(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      spacing: 8,
                      children: [
                        Icon(
                          Icons.info_outline_rounded,
                          size: 18,
                          color: c.textMuted,
                        ),
                        Expanded(
                          child: Text(Strings.changedOutside, style: s.note),
                        ),
                      ],
                    ),
                  ),
                if (section?.loadError case final error?)
                  Semantics(
                    liveRegion: true,
                    child: Text(error, style: s.note.copyWith(color: c.error)),
                  )
                else if (section != null &&
                    section.loaded &&
                    panel.body != null)
                  panel.body!(context),
              ],
            ),
          ),
        ),
      ),
    );
  }

  Widget _bar(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final canSave = _state.dirty && !_state.saving;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 16),
      decoration: BoxDecoration(
        color: c.background,
        border: Border(top: BorderSide(color: c.border)),
      ),
      child: Center(
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 760),
          child: Row(
            spacing: 12,
            children: [
              Expanded(
                child: AnimatedSwitcher(
                  duration: Motion.of(context).fieldError,
                  switchInCurve: Motion.enter,
                  switchOutCurve: Motion.enter,
                  layoutBuilder: (current, previous) => Stack(
                    alignment: Alignment.centerLeft,
                    children: [...previous, ?current],
                  ),
                  child: switch ((_state.barError, _state.saving)) {
                    (final String error, _) => Semantics(
                      key: ValueKey(error),
                      liveRegion: true,
                      child: Text(
                        error,
                        style: s.note.copyWith(color: c.error),
                      ),
                    ),
                    (_, true) => Text(
                      Strings.saving,
                      key: const ValueKey(Strings.saving),
                      style: s.note,
                    ),
                    _ => const SizedBox.shrink(),
                  },
                ),
              ),
              Pressable(
                onPressed: _state.dirty && !_state.saving
                    ? _state.discard
                    : null,
                borderColor: c.border,
                minHeight: 44,
                padding: const EdgeInsets.symmetric(horizontal: 16),
                child: Center(
                  widthFactor: 1,
                  child: Text(
                    Strings.cancel,
                    style: _state.dirty && !_state.saving
                        ? s.row
                        : s.row.copyWith(color: c.textMuted),
                  ),
                ),
              ),
              Pressable(
                onPressed: canSave ? _state.save : null,
                color: canSave ? c.accent : c.field,
                hoverColor: c.accent,
                minHeight: 44,
                padding: const EdgeInsets.symmetric(horizontal: 16),
                child: Center(
                  widthFactor: 1,
                  child: Text(
                    Strings.save,
                    style: s.primaryButton.copyWith(
                      color: canSave ? c.textOnAccent : c.textMuted,
                    ),
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
