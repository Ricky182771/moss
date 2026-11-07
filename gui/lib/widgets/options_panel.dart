import 'package:flutter/material.dart';

import '../models/settings.dart';
import '../state/app_state.dart';
import '../state/options_state.dart';
import '../strings.dart';
import '../theme/colors.dart';
import '../theme/motion.dart';
import '../theme/theme.dart';
import 'controls.dart';
import 'list_keys.dart';
import 'popover.dart';

/// Alto de una fila de la lista (40) más su separación (2).
const _rowExtent = 42.0;

/// La ventana de Opciones (DISENO.md → Chat → Opciones): el modelo, con su
/// proveedor, y el interruptor de la búsqueda web. El enlace a Configuración
/// del modelo llega con Configuración (G3).
class OptionsPanel extends StatefulWidget {
  final AppState app;

  /// De `/model` sin modelo; null al abrir con el engranaje.
  final OptionsRequest? request;
  final VoidCallback onClose;

  const OptionsPanel({
    super.key,
    required this.app,
    required this.request,
    required this.onClose,
  });

  @override
  State<OptionsPanel> createState() => _OptionsPanelState();
}

class _OptionsPanelState extends State<OptionsPanel> {
  late final _state = OptionsState(widget.app)..addListener(_changed);
  final _filter = TextEditingController();
  final _scroll = ScrollController();
  int _highlight = -1;

  /// Lo que se ve la última vez: si cambia, el resaltado vuelve a empezar.
  (bool, String?, List<String>?)? _shownList;

  @override
  void initState() {
    super.initState();
    _filter.addListener(() => setState(_resetHighlight));
    final request = widget.request;
    _state.load(
      provider: request?.provider,
      providers: request != null && request.provider == null,
    );
  }

  void _changed() {
    final now = (_state.pickingProvider, _state.shown, _state.models);
    if (now == _shownList) return setState(() {});
    _shownList = now;
    _filter.clear();
    setState(_resetHighlight);
  }

  @override
  void dispose() {
    _state.dispose();
    _filter.dispose();
    _scroll.dispose();
    super.dispose();
  }

  // Lo que muestra la lista --------------------------------------------------

  List<ProviderChoice> get _providers {
    final q = _filter.text.trim().toLowerCase();
    return [
      for (final p in _state.settings?.providers ?? const <ProviderChoice>[])
        if (p.name.toLowerCase().contains(q)) p,
    ];
  }

  List<String> get _models {
    final q = _filter.text.trim().toLowerCase();
    return [
      for (final m in _state.models ?? const <String>[])
        if (m.toLowerCase().contains(q)) m,
    ];
  }

  int get _count => _state.pickingProvider ? _providers.length : _models.length;

  bool _enabled(int i) {
    final s = _state.settings;
    if (s == null) return false;
    return _state.pickingProvider
        ? _providers[i].selectable && !s.baseUrlLocked
        : !s.modelLocked;
  }

  void _resetHighlight() {
    final current = _state.pickingProvider
        ? _providers.indexWhere((p) => p.id == _state.shown)
        : _models.indexWhere(_state.isCurrent);
    _highlight = current >= 0 && _enabled(current)
        ? current
        : firstEnabled(_count, _enabled);
    WidgetsBinding.instance.addPostFrameCallback(
      (_) => revealRow(_scroll, _highlight, _rowExtent),
    );
  }

  void _move(int i) {
    setState(() => _highlight = i);
    revealRow(_scroll, i, _rowExtent);
  }

  void _activate(int i) {
    if (i < 0 || i >= _count || !_enabled(i)) return;
    if (_state.pickingProvider) {
      _state.showModels(_providers[i].id);
    } else {
      _pick(_models[i]);
    }
  }

  Future<void> _pick(String model) async {
    if (model.trim().isEmpty) return;
    if (await _state.pick(model.trim()) && mounted) widget.onClose();
  }

  void _toggleProviders() {
    if (_state.pickingProvider) {
      final shown = _state.shown;
      if (shown != null) _state.showModels(shown);
    } else {
      _state.showProviders();
    }
  }

  // Dibujo -------------------------------------------------------------------

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final settings = _state.settings;
    final shownName =
        settings?.providers
            .where((p) => p.id == _state.shown)
            .firstOrNull
            ?.name ??
        _state.shown ??
        '';
    final error = s.help.copyWith(color: c.error);
    final note = s.help;

    final Widget body;
    if (_state.loadError case final message?) {
      body = Padding(
        padding: const EdgeInsets.all(8),
        child: Text(message, style: error),
      );
    } else if (settings == null) {
      body = const SizedBox(height: 40);
    } else {
      body = Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.stretch,
        spacing: 4,
        children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(8, 8, 8, 4),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.stretch,
              spacing: 8,
              children: [
                Text(Strings.modelSection.toUpperCase(), style: s.group),
                _FieldBox(
                  child: Pressable(
                    onPressed: settings.baseUrlLocked ? null : _toggleProviders,
                    radius: 10,
                    minHeight: 40,
                    padding: const EdgeInsets.symmetric(horizontal: 12),
                    child: Semantics(
                      label: Strings.provider,
                      expanded: _state.pickingProvider,
                      child: Row(
                        spacing: 8,
                        children: [
                          Expanded(child: Text(shownName, style: s.row)),
                          Icon(
                            _state.pickingProvider
                                ? Icons.expand_less_rounded
                                : Icons.expand_more_rounded,
                            size: 16,
                            color: c.textMuted,
                          ),
                        ],
                      ),
                    ),
                  ),
                ),
                if (settings.baseUrlLocked)
                  Text(Strings.lockedBy('CHAT_BASE_URL'), style: note),
                _FieldBox(
                  child: Row(
                    spacing: 8,
                    children: [
                      const SizedBox(width: 4),
                      Icon(Icons.search_rounded, size: 16, color: c.textMuted),
                      Expanded(
                        child: Focus(
                          onKeyEvent: (_, e) => listKeys(
                            e,
                            count: _count,
                            index: _highlight,
                            move: _move,
                            enabled: _enabled,
                            activate: () => _activate(_highlight),
                          ),
                          child: TextField(
                            controller: _filter,
                            autofocus: true,
                            style: s.row,
                            cursorColor: c.accent,
                            decoration: InputDecoration(
                              isDense: true,
                              border: InputBorder.none,
                              contentPadding: const EdgeInsets.symmetric(
                                vertical: 11,
                              ),
                              hintText: _state.pickingProvider
                                  ? Strings.filterProviders
                                  : Strings.filterModels,
                              hintStyle: s.row.copyWith(color: c.textMuted),
                            ),
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),
          _list(shownName),
          if (settings.modelLocked && !_state.pickingProvider)
            _Note(
              _state.modelNote.isEmpty
                  ? Strings.lockedBy('CHAT_MODEL')
                  : _state.modelNote,
              style: note,
            ),
          if (_state.switchError case final message?)
            _Note(message, style: error, live: true),
          Container(
            height: 1,
            margin: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
            color: c.border,
          ),
          _searchSwitch(),
        ],
      );
    }
    return PopoverFrame(
      width: 360,
      padding: const EdgeInsets.all(8),
      radius: 14,
      color: c.sidebar,
      child: Semantics(
        scopesRoute: true,
        namesRoute: true,
        explicitChildNodes: true,
        label: Strings.options,
        child: body,
      ),
    );
  }

  Widget _list(String shownName) {
    final c = context.colors;
    final s = context.styles;
    if (!_state.pickingProvider) {
      if (_state.modelsError case final message?) {
        return Padding(
          padding: const EdgeInsets.symmetric(horizontal: 8),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            spacing: 8,
            children: [
              Semantics(
                liveRegion: true,
                child: Text(message, style: s.help.copyWith(color: c.error)),
              ),
              _FieldBox(
                child: Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 12),
                  child: TextField(
                    enabled: !(_state.settings?.modelLocked ?? false),
                    style: s.model,
                    cursorColor: c.accent,
                    onSubmitted: _pick,
                    decoration: InputDecoration(
                      isDense: true,
                      border: InputBorder.none,
                      contentPadding: const EdgeInsets.symmetric(vertical: 12),
                      hintText: Strings.typeModel,
                      hintStyle: s.row.copyWith(color: c.textMuted),
                    ),
                  ),
                ),
              ),
            ],
          ),
        );
      }
      if (_state.models == null) {
        return SizedBox(
          height: 40,
          child: _state.loadingVisible
              ? Semantics(
                  liveRegion: true,
                  child: Row(
                    spacing: 10,
                    children: [
                      const SizedBox(width: 10),
                      SizedBox.square(
                        dimension: 14,
                        // Fijo con Reducir animaciones.
                        child: CircularProgressIndicator(
                          value: Motion.of(context).reduced ? 0.75 : null,
                          strokeWidth: 2,
                          color: c.textMuted,
                        ),
                      ),
                      Text(
                        Strings.loadingModels,
                        style: s.row.copyWith(color: c.textMuted),
                      ),
                    ],
                  ),
                )
              : null,
        );
      }
    }
    if (_count == 0) {
      return _Note(Strings.noMatches, style: s.help);
    }
    return Semantics(
      label: _state.pickingProvider
          ? Strings.providers
          : Strings.modelsOf(shownName),
      container: true,
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxHeight: 200),
        child: ExcludeFocus(
          // Las filas se eligen con ↑ ↓ desde el filtro o con el mouse.
          child: ListView.builder(
            controller: _scroll,
            shrinkWrap: true,
            padding: const EdgeInsets.symmetric(horizontal: 4),
            itemExtent: _rowExtent,
            itemCount: _count,
            itemBuilder: (context, i) {
              if (_state.pickingProvider) {
                final p = _providers[i];
                return _Row(
                  label: p.name,
                  note: p.note,
                  selected: p.id == _state.shown,
                  highlighted: i == _highlight,
                  enabled: _enabled(i),
                  onTap: () => _activate(i),
                );
              }
              final m = _models[i];
              return _Row(
                label: m,
                mono: true,
                selected: _state.isCurrent(m),
                highlighted: i == _highlight,
                enabled: _enabled(i),
                onTap: () => _activate(i),
              );
            },
          ),
        ),
      ),
    );
  }

  Widget _searchSwitch() {
    final c = context.colors;
    final s = context.styles;
    final enabled = _state.hasSearchKey;
    return ValueListenableBuilder(
      valueListenable: widget.app.searchOn,
      builder: (context, on, _) {
        void toggle() => widget.app.searchOn.value = !on;
        return MergeSemantics(
          child: GestureDetector(
            onTap: enabled ? toggle : null,
            behavior: HitTestBehavior.opaque,
            child: Container(
              constraints: const BoxConstraints(minHeight: 52),
              padding: const EdgeInsets.symmetric(horizontal: 10),
              child: Row(
                spacing: 12,
                children: [
                  Icon(Icons.language_rounded, size: 18, color: c.textMuted),
                  Expanded(
                    child: Column(
                      mainAxisSize: MainAxisSize.min,
                      crossAxisAlignment: CrossAxisAlignment.start,
                      spacing: 2,
                      children: [
                        Text(
                          Strings.searchWeb,
                          style: enabled
                              ? s.nav
                              : s.nav.copyWith(color: c.textMuted),
                        ),
                        Text(
                          enabled
                              ? Strings.searchWebHint
                              : _state.searchNote.isEmpty
                              ? Strings.noSearchKey
                              : _state.searchNote,
                          style: s.help,
                        ),
                      ],
                    ),
                  ),
                  Switch(
                    value: on && enabled,
                    onChanged: enabled ? (_) => toggle() : null,
                    thumbColor: WidgetStateProperty.resolveWith(
                      (st) => st.contains(WidgetState.selected)
                          ? c.textOnAccent
                          : c.textMuted,
                    ),
                    trackColor: WidgetStateProperty.resolveWith(
                      (st) => st.contains(WidgetState.selected)
                          ? c.accent
                          : c.field,
                    ),
                    trackOutlineColor: WidgetStatePropertyAll(c.border),
                  ),
                ],
              ),
            ),
          ),
        );
      },
    );
  }
}

/// Campo de 40 px con el fondo de campo y borde.
class _FieldBox extends StatelessWidget {
  final Widget child;
  const _FieldBox({required this.child});

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    return Container(
      constraints: const BoxConstraints(minHeight: 40),
      decoration: BoxDecoration(
        color: c.field,
        borderRadius: BorderRadius.circular(10),
        border: Border.all(color: c.border),
      ),
      child: child,
    );
  }
}

class _Note extends StatelessWidget {
  final String text;
  final TextStyle style;
  final bool live;
  const _Note(this.text, {required this.style, this.live = false});

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
    child: Semantics(
      liveRegion: live,
      child: Text(text, style: style),
    ),
  );
}

/// Una fila de la lista: el actual con el acento suave y la palomita; la
/// resaltada con el teclado, con el anillo del foco.
class _Row extends StatelessWidget {
  final String label;
  final String note;
  final bool mono;
  final bool selected;
  final bool highlighted;
  final bool enabled;
  final VoidCallback onTap;

  const _Row({
    required this.label,
    required this.selected,
    required this.highlighted,
    required this.enabled,
    required this.onTap,
    this.note = '',
    this.mono = false,
  });

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final base = mono ? s.model : s.row;
    return Padding(
      padding: const EdgeInsets.only(bottom: 2),
      child: Container(
        foregroundDecoration: highlighted
            ? BoxDecoration(
                border: Border.all(color: c.accent, width: c.focusWidth),
                borderRadius: BorderRadius.circular(8),
              )
            : null,
        child: Pressable(
          onPressed: enabled ? onTap : null,
          radius: 8,
          minHeight: 40,
          padding: const EdgeInsets.symmetric(horizontal: 10),
          selected: selected,
          color: selected ? c.accentSoft : null,
          hoverColor: selected ? c.accentSoft : c.field,
          child: Row(
            spacing: 10,
            children: [
              Expanded(
                child: Text(
                  label,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: enabled ? base : base.copyWith(color: c.textMuted),
                ),
              ),
              if (note.isNotEmpty) Text(note, style: s.help),
              if (selected)
                Icon(Icons.check_rounded, size: 16, color: c.accent),
            ],
          ),
        ),
      ),
    );
  }
}
