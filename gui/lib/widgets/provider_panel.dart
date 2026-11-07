import 'package:flutter/material.dart';

import '../state/provider_section.dart';
import '../strings.dart';
import '../theme/colors.dart';
import '../theme/motion.dart';
import '../theme/theme.dart';
import 'controls.dart';
import 'delete_dialog.dart';
import 'settings_form.dart';

/// Proveedor de IA (maqueta `Config-proveedor`): la lista con sus dos grupos,
/// el elegido y su modelo.
class ProviderPanel extends StatefulWidget {
  final ProviderSection section;
  const ProviderPanel(this.section, {super.key});

  @override
  State<ProviderPanel> createState() => _ProviderPanelState();
}

class _ProviderPanelState extends State<ProviderPanel> {
  final _filter = TextEditingController();
  final _nameFocus = FocusNode();

  ProviderSection get p => widget.section;

  @override
  void dispose() {
    _filter.dispose();
    _nameFocus.dispose();
    super.dispose();
  }

  void _add() {
    p.addEndpoint();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted) _nameFocus.requestFocus();
    });
  }

  Future<void> _delete(ProviderRow row) => confirm(
    context,
    question: Strings.deleteEndpointQuestion(row.name),
    confirmLabel: Strings.confirmDelete,
    action: () => p.deleteEndpoint(row.id),
  );

  @override
  Widget build(BuildContext context) {
    final s = context.styles;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      spacing: 24,
      children: [
        SettingsCard(
          title: Strings.providerMenu,
          trailing: Text(Strings.providersNote, style: s.note),
          spacing: 16,
          children: [
            _list(context),
            if (p.baseUrlLocked)
              Text(Strings.lockedBy(Strings.envBaseUrl), style: s.note),
          ],
        ),
        _chosen(context),
        _model(context),
      ],
    );
  }

  Widget _list(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final builtin = p.providers.where((r) => !r.endpoint);
    final endpoints = p.providers.where((r) => r.endpoint);
    Widget group(String text, {double top = 8}) => Padding(
      padding: EdgeInsets.fromLTRB(12, top, 12, 4),
      child: Semantics(
        header: true,
        child: Text(text.toUpperCase(), style: s.group),
      ),
    );
    return Container(
      constraints: const BoxConstraints(maxHeight: 296),
      decoration: BoxDecoration(
        color: c.field,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: c.border),
      ),
      child: Semantics(
        label: Strings.providers,
        container: true,
        explicitChildNodes: true,
        child: ListView(
          shrinkWrap: true,
          padding: const EdgeInsets.all(6),
          children: [
            group(Strings.fromTable),
            for (final r in builtin) _row(context, r),
            group(Strings.yourEndpoints, top: 12),
            for (final r in endpoints) _row(context, r),
            if (p.provider == ProviderSection.newEndpoint)
              _ListRow(
                label: Strings.newEndpoint,
                note: '',
                selected: true,
                onTap: () {},
              ),
            Pressable(
              onPressed: p.baseUrlLocked ? null : _add,
              minHeight: 44,
              padding: const EdgeInsets.symmetric(horizontal: 12),
              child: Row(
                spacing: 10,
                children: [
                  Icon(
                    Icons.add_rounded,
                    size: 16,
                    color: p.baseUrlLocked ? c.textMuted : c.accent,
                  ),
                  Flexible(
                    child: Text(
                      Strings.addEndpoint,
                      overflow: TextOverflow.ellipsis,
                      style: s.nav.copyWith(
                        color: p.baseUrlLocked ? c.textMuted : c.accent,
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _row(BuildContext context, ProviderRow r) {
    final inUse = r.id == p.saved.provider;
    final noKey = r.note.isNotEmpty;
    return _ListRow(
      label: r.name,
      note: [
        if (inUse) Strings.inUse,
        if (!r.needsKey)
          Strings.local
        else if (r.keySaved)
          Strings.keySaved
        else
          r.note,
      ].where((t) => t.isNotEmpty).join(' · '),
      dot: inUse,
      muted: noKey,
      selected: r.id == p.provider,
      // Con CHAT_BASE_URL el proveedor no se cambia.
      onTap: p.baseUrlLocked && !inUse ? null : () => p.select(r.id),
    );
  }

  Widget _chosen(BuildContext context) {
    final s = context.styles;
    final c = context.colors;
    final row = p.row(p.provider);
    final isNew = p.provider == ProviderSection.newEndpoint;
    final inUse = p.provider == p.saved.provider;
    final keyLock = p.keyLocked ? Strings.envApiKey : null;
    final status = inUse
        ? p.keyStatus
        : row?.keySaved ?? false
        ? Strings.keySaved
        : '';
    return SettingsCard(
      title: isNew ? Strings.newEndpoint : row?.name ?? p.provider,
      trailing: isNew ? null : Text(p.provider, style: s.noteMono),
      children: [
        if (p.isEndpoint) ...[
          LabeledField(
            label: Strings.endpointName,
            controller: p.name,
            focusNode: _nameFocus,
            onChanged: (_) => p.edited(),
          ),
          LabeledField(
            label: Strings.baseUrl,
            controller: p.baseUrl,
            mono: true,
            lockedBy: p.baseUrlLocked ? Strings.envBaseUrl : null,
            onChanged: (_) => p.edited(),
            help: p.hint ?? Strings.baseUrlHelp,
          ),
        ],
        SecretField(
          label: Strings.apiKey,
          controller: p.apiKey,
          status: status,
          lockedBy: keyLock,
          onChanged: (_) => p.edited(),
        ),
        if (!p.isEndpoint && p.hint != null) Text(p.hint!, style: s.note),
        // `check` da un solo error, sin decir de qué campo: va al final de
        // lo que se edita, antes de Borrar.
        FieldMessage(p.error),
        if (p.isEndpoint && !isNew && row != null)
          Container(
            padding: const EdgeInsets.only(top: 16),
            decoration: BoxDecoration(
              border: Border(top: BorderSide(color: c.border)),
            ),
            child: Wrap(
              crossAxisAlignment: WrapCrossAlignment.center,
              spacing: 12,
              runSpacing: 8,
              children: [
                Pressable(
                  onPressed: inUse ? null : () => _delete(row),
                  borderColor: c.border,
                  minHeight: 44,
                  padding: const EdgeInsets.symmetric(horizontal: 16),
                  child: Row(
                    mainAxisSize: MainAxisSize.min,
                    spacing: 8,
                    children: [
                      Icon(
                        Icons.delete_outline_rounded,
                        size: 16,
                        color: inUse ? c.textMuted : c.error,
                      ),
                      Text(
                        Strings.deleteEndpoint,
                        style: s.row.copyWith(
                          color: inUse ? c.textMuted : c.error,
                        ),
                      ),
                    ],
                  ),
                ),
                if (inUse) Text(Strings.endpointInUse, style: s.note),
              ],
            ),
          ),
      ],
    );
  }

  Widget _model(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final locked = p.modelLocked;
    final all = p.models ?? const <String>[];
    final q = _filter.text.trim().toLowerCase();
    final shown = q.isEmpty
        ? all
        : all.where((m) => m.toLowerCase().contains(q)).toList();
    final String note;
    if (p.loadingVisible) {
      note = Strings.loadingModels;
    } else if (p.models != null) {
      note = Strings.modelsCount(all.length);
    } else {
      note = '';
    }
    return SettingsCard(
      title: Strings.modelSection,
      children: [
        Wrap(
          spacing: 10,
          runSpacing: 10,
          children: [
            ConstrainedBox(
              constraints: const BoxConstraints(minWidth: 240, maxWidth: 520),
              child: FieldBox(
                controller: _filter,
                label: Strings.filterModels,
                hint: Strings.filterModels,
                enabled: !locked,
                onChanged: (_) => setState(() {}),
              ),
            ),
            Pressable(
              onPressed: locked || !p.canRequestModels ? null : p.loadModels,
              borderColor: c.border,
              minHeight: 44,
              padding: const EdgeInsets.symmetric(horizontal: 16),
              child: Row(
                mainAxisSize: MainAxisSize.min,
                spacing: 8,
                children: [
                  Icon(
                    Icons.refresh_rounded,
                    size: 16,
                    color: p.canRequestModels ? c.text : c.textMuted,
                  ),
                  Text(
                    Strings.refresh,
                    style: p.canRequestModels
                        ? s.row
                        : s.row.copyWith(color: c.textMuted),
                  ),
                ],
              ),
            ),
          ],
        ),
        if (p.models != null && shown.isNotEmpty)
          Container(
            constraints: const BoxConstraints(maxHeight: 300),
            decoration: BoxDecoration(
              color: c.field,
              borderRadius: BorderRadius.circular(12),
              border: Border.all(color: c.border),
            ),
            child: Semantics(
              label: Strings.modelsOf(p.row(p.provider)?.name ?? p.name.text),
              container: true,
              explicitChildNodes: true,
              child: ListView.builder(
                shrinkWrap: true,
                padding: const EdgeInsets.all(6),
                itemCount: shown.length,
                itemBuilder: (context, i) => _ListRow(
                  label: shown[i],
                  mono: true,
                  radio: true,
                  selected: shown[i] == p.model.text,
                  onTap: locked ? null : () => p.pickModel(shown[i]),
                ),
              ),
            ),
          ),
        if (p.loadingVisible)
          Semantics(
            liveRegion: true,
            child: Row(
              spacing: 8,
              children: [
                SizedBox.square(
                  dimension: 14,
                  // Fijo con Reducir animaciones.
                  child: CircularProgressIndicator(
                    value: Motion.of(context).reduced ? 0.75 : null,
                    strokeWidth: 2,
                    color: c.textMuted,
                  ),
                ),
                Text(note, style: s.note),
              ],
            ),
          )
        else if (p.modelsError case final error?)
          FieldMessage(error)
        else if (note.isNotEmpty)
          Text(note, style: s.note),
        LabeledField(
          label: Strings.modelByHand,
          controller: p.model,
          mono: true,
          hint: Strings.typeModelName,
          lockedBy: locked ? Strings.envModel : null,
          onChanged: (_) => p.edited(),
        ),
      ],
    );
  }
}

/// Fila de la lista de proveedores o de modelos (44 px).
class _ListRow extends StatelessWidget {
  final String label;
  final String note;
  final bool selected;
  final bool dot;
  final bool muted;
  final bool mono;
  final bool radio;
  final VoidCallback? onTap;

  const _ListRow({
    required this.label,
    this.note = '',
    required this.selected,
    this.dot = false,
    this.muted = false,
    this.mono = false,
    this.radio = false,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final text = mono ? s.model : (selected ? s.rowOpen : s.row);
    return Semantics(
      selected: selected,
      inMutuallyExclusiveGroup: true,
      child: Pressable(
        onPressed: onTap,
        selected: selected,
        color: selected ? c.accentSoft : null,
        minHeight: 44,
        padding: const EdgeInsets.symmetric(horizontal: 12),
        child: Row(
          spacing: 12,
          children: [
            if (radio) RadioMark(selected: selected),
            if (dot)
              Container(
                width: 8,
                height: 8,
                decoration: BoxDecoration(
                  color: c.accent,
                  shape: BoxShape.circle,
                ),
              ),
            Expanded(
              child: Text(
                label,
                style: muted ? text.copyWith(color: c.textMuted) : text,
                overflow: TextOverflow.ellipsis,
              ),
            ),
            if (note.isNotEmpty)
              Flexible(
                child: Text(
                  note,
                  style: s.help,
                  textAlign: TextAlign.end,
                  overflow: TextOverflow.ellipsis,
                ),
              ),
          ],
        ),
      ),
    );
  }
}
