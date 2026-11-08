import 'package:flutter/material.dart';

import '../state/telegram_section.dart';
import '../strings.dart';
import '../theme/colors.dart';
import '../theme/theme.dart';
import 'delete_dialog.dart';
import 'log_dialog.dart';
import 'settings_form.dart';

/// El color del punto del servicio, según `active` de `service.status`.
Color serviceDot(MossColors c, ServiceStatus? status) =>
    switch (status?.active) {
      'active' => c.accent,
      'failed' => c.error,
      _ => c.textMuted,
    };

/// Telegram (maqueta `Config-addons`): Servicio, Bot, Acceso, Límites e
/// Invitados. Solo Linux; en Windows, solo el aviso.
class TelegramPanel extends StatefulWidget {
  final TelegramSection section;
  const TelegramPanel(this.section, {super.key});

  @override
  State<TelegramPanel> createState() => _TelegramPanelState();
}

class _TelegramPanelState extends State<TelegramPanel> {
  /// Ver registro: al cerrar la ventana, el foco regresa aquí.
  final _viewLog = FocusNode(debugLabel: 'Ver registro');

  @override
  void dispose() {
    _viewLog.dispose();
    super.dispose();
  }

  void _openLog() => showLogDialog(
    context,
    rpc: widget.section.rpc,
    returnFocus: _viewLog,
    now: widget.section.app.now,
  );

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final t = widget.section;
    if (!t.supported) {
      return SettingsCard(
        children: [Text(Strings.telegramLinuxOnly, style: s.row)],
      );
    }
    final problem = t.readOnlyReason ?? t.fileError;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      spacing: 24,
      children: [
        if (problem != null)
          Semantics(
            liveRegion: true,
            child: Text(problem, style: s.note.copyWith(color: c.error)),
          ),
        if (t.envNote.isNotEmpty) Text(t.envNote, style: s.note),
        _ServiceCard(t, onViewLog: _openLog, viewLogFocus: _viewLog),
        _BotCard(t),
        _AccessCard(t),
        _LimitsCard(t),
        _InvitedCard(t),
        if (t.formError != null && t.fieldErrors.isEmpty)
          FieldMessage(t.formError),
      ],
    );
  }
}

class _ServiceCard extends StatelessWidget {
  final TelegramSection t;
  final VoidCallback? onViewLog;
  final FocusNode? viewLogFocus;
  const _ServiceCard(this.t, {this.onViewLog, this.viewLogFocus});

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final status = t.status;
    final install = t.install;
    final error = s.note.copyWith(color: c.error);
    Widget column(String label, Widget value) => Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      spacing: 6,
      children: [
        Text(label, style: s.note),
        value,
      ],
    );
    final waitingStatus = t.waiting.contains(TelegramAction.status);
    return SettingsCard(
      title: Strings.serviceSection,
      children: [
        Wrap(
          spacing: 16,
          runSpacing: 16,
          children: [
            SizedBox(
              width: 300,
              child: column(Strings.installedLabel, switch (install) {
                null => const SizedBox(height: 20),
                AddonInstall(problem: final p) when p.isNotEmpty => Text(
                  p,
                  style: error,
                ),
                AddonInstall(:final path) => Text(path ?? '', style: s.model),
              }),
            ),
            SizedBox(
              width: 300,
              child: column(
                Strings.serviceLabel,
                Semantics(
                  liveRegion: true,
                  child: Row(
                    spacing: 8,
                    children: [
                      Container(
                        width: 8,
                        height: 8,
                        decoration: BoxDecoration(
                          color: serviceDot(c, status),
                          shape: BoxShape.circle,
                        ),
                      ),
                      Flexible(
                        child: Text(
                          t.statusError ?? status?.summary ?? '',
                          style: t.statusError != null ? error : s.row,
                        ),
                      ),
                    ],
                  ),
                ),
              ),
            ),
          ],
        ),
        if (status != null && status.problem.isNotEmpty)
          Text(status.problem, style: error),
        if (status != null && status.mismatch.isNotEmpty)
          Text(status.mismatch, style: error),
        if (status?.lingerNotice case final notice?)
          Text(notice, style: s.note),
        if (status?.lingerCommand case final command?)
          SelectableText(command, style: s.noteMono),
        Wrap(
          spacing: 10,
          runSpacing: 10,
          children: [
            // Sin systemd de usuario (o sin dónde ponerlo), el mensaje ya
            // está arriba: no hay nada que reiniciar ni que actualizar.
            if (t.statusError == null && (status?.systemd ?? true)) ...[
              SecondaryButton(
                label: Strings.restartService,
                icon: Icons.restart_alt_rounded,
                onPressed: status == null ? null : t.restart,
                waiting: t.waiting.contains(TelegramAction.restart),
                slow: t.slow.contains(TelegramAction.restart),
              ),
              SecondaryButton(
                label: Strings.refreshStatus,
                bordered: false,
                onPressed: t.refreshStatus,
                waiting: waitingStatus,
                slow: t.slow.contains(TelegramAction.status),
              ),
            ],
            if (onViewLog != null)
              SecondaryButton(
                focusNode: viewLogFocus,
                label: Strings.viewLog,
                icon: Icons.article_outlined,
                bordered: false,
                onPressed: onViewLog,
              ),
          ],
        ),
        FieldMessage(t.actionError),
      ],
    );
  }
}

/// Un campo de texto del formulario, con la etiqueta de `labels`.
class _Field extends StatelessWidget {
  final TelegramSection t;
  final String field;
  final String? help;
  final Widget? trailing;
  const _Field(this.t, this.field, {this.help, this.trailing});

  @override
  Widget build(BuildContext context) => LabeledField(
    label: t.label(field),
    controller: t.fields[field]!,
    onChanged: (_) => t.edited(field),
    mono: true,
    enabled: !t.readOnly,
    help: help,
    error: t.fieldErrors[field],
    trailing: trailing,
  );
}

/// Un campo con una lista para elegir, y que se puede escribir a mano.
class _Choice extends StatelessWidget {
  final TelegramSection t;
  final String field;
  final List<String> options;
  final String? hint;
  final String? help;
  final VoidCallback? onFocus;
  const _Choice(
    this.t,
    this.field, {
    required this.options,
    this.hint,
    this.help,
    this.onFocus,
  });

  @override
  Widget build(BuildContext context) => ChoiceField(
    label: t.label(field),
    controller: t.fields[field]!,
    options: options,
    onChanged: (_) => t.edited(field),
    enabled: !t.readOnly,
    hint: hint,
    help: help,
    error: t.fieldErrors[field],
    onFocus: onFocus,
  );
}

/// Una key del bot, con Quitar (usa la de la app).
class _Secret extends StatelessWidget {
  final TelegramSection t;
  final String field;
  final bool removable;
  final String? help;
  final String hint;
  const _Secret(
    this.t,
    this.field, {
    this.removable = true,
    this.help,
    this.hint = Strings.optionalHint,
  });

  @override
  Widget build(BuildContext context) {
    final removing = switch (field) {
      'model_key' => t.removeModelKey,
      _ => t.removeSearchKey,
    };
    return SecretField(
      label: t.label(field),
      controller: t.fields[field]!,
      onChanged: (_) => t.edited(field),
      enabled: !t.readOnly,
      status: t.secretStatus(field),
      hint: hint,
      help: help,
      error: t.fieldErrors[field],
      trailing: removable
          ? SecondaryButton(
              label: removing ? Strings.keepSavedKey : Strings.removeKey,
              onPressed: t.readOnly ? null : () => t.toggleRemove(field),
            )
          : null,
    );
  }
}

class _BotCard extends StatelessWidget {
  final TelegramSection t;
  const _BotCard(this.t);

  @override
  Widget build(BuildContext context) {
    final s = context.styles;
    return SettingsCard(
      title: Strings.botSection,
      children: [
        CheckRow(
          value: t.enabled,
          onChanged: t.readOnly ? null : t.setEnabled,
          title: Strings.enabledTitle,
          note: Strings.enabledNote,
        ),
        _Secret(
          t,
          'token',
          removable: false,
          hint: Strings.tokenHint,
          help: Strings.keepTokenHelp,
        ),
        _Choice(
          t,
          'model',
          options: t.models,
          hint: t.text('same_model'),
          help: t.modelsError,
          onFocus: t.loadModels,
        ),
        _Secret(t, 'model_key'),
        _Secret(t, 'search_key'),
        Text(t.text('own_keys_note'), style: s.note),
      ],
    );
  }
}

class _AccessCard extends StatelessWidget {
  final TelegramSection t;
  const _AccessCard(this.t);

  @override
  Widget build(BuildContext context) => SettingsCard(
    title: Strings.accessSection,
    children: [
      _Field(t, 'allowed_users', help: Strings.allowedUsersHelp),
      _Field(
        t,
        'invite_code',
        help: Strings.inviteCodeHelp,
        trailing: SecondaryButton(
          label: Strings.generateCode,
          icon: Icons.key_rounded,
          onPressed: t.readOnly ? null : t.generateInviteCode,
        ),
      ),
    ],
  );
}

class _LimitsCard extends StatelessWidget {
  final TelegramSection t;
  const _LimitsCard(this.t);

  static const _fields = [
    'daily_limit',
    'concurrent',
    'idle_minutes',
    'stale_minutes',
    'draft_ms',
    'time_zone',
  ];

  @override
  Widget build(BuildContext context) => SettingsCard(
    title: Strings.limitsSection,
    children: [
      // La rejilla de la maqueta: columnas de 200 px como mínimo.
      LayoutBuilder(
        builder: (context, box) {
          const gap = 16.0;
          final columns = ((box.maxWidth + gap) / (200 + gap)).floor().clamp(
            1,
            3,
          );
          final width = (box.maxWidth - gap * (columns - 1)) / columns;
          return Wrap(
            spacing: gap,
            runSpacing: gap,
            children: [
              for (final f in _fields)
                SizedBox(
                  width: width,
                  child: f == 'time_zone'
                      ? _Choice(t, f, options: t.timeZones)
                      : _Field(t, f),
                ),
            ],
          );
        },
      ),
      CheckRow(
        value: t.logErrorDetails,
        onChanged: t.readOnly ? null : t.setLogErrorDetails,
        title: Strings.logDetailsTitle,
        note: t.text('log_details_note'),
      ),
    ],
  );
}

class _InvitedCard extends StatelessWidget {
  final TelegramSection t;
  const _InvitedCard(this.t);

  Future<void> _remove(BuildContext context, String user) async {
    final yes = await confirm(
      context,
      question: t.removeQuestions[user] ?? '',
      confirmLabel: Strings.removeInvited,
      action: () async => null,
    );
    if (yes) await t.removeInvited(user);
  }

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    return SettingsCard(
      title: Strings.invitedTitle(t.invited.length),
      children: [
        if (t.invitedError case final error?)
          Semantics(
            liveRegion: true,
            child: Text(error, style: s.note.copyWith(color: c.error)),
          ),
        if (t.invited.isEmpty && t.invitedError == null)
          Container(
            padding: const EdgeInsets.all(24),
            decoration: BoxDecoration(
              color: c.field,
              borderRadius: BorderRadius.circular(12),
            ),
            child: Column(
              spacing: 6,
              children: [
                Text(Strings.noInvited, style: s.option),
                Text(
                  Strings.noInvitedNote,
                  style: s.note,
                  textAlign: TextAlign.center,
                ),
              ],
            ),
          ),
        for (final user in t.invited)
          Row(
            spacing: 12,
            children: [
              Expanded(child: Text(user, style: s.model)),
              Semantics(
                label: Strings.removeInvitedLabel(user),
                excludeSemantics: true,
                button: true,
                child: SecondaryButton(
                  label: Strings.removeInvited,
                  icon: Icons.person_remove_outlined,
                  onPressed: () => _remove(context, user),
                  waiting: t.waiting.contains(TelegramAction.remove(user)),
                  slow: t.slow.contains(TelegramAction.remove(user)),
                ),
              ),
            ],
          ),
      ],
    );
  }
}
