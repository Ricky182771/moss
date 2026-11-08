import 'package:flutter/material.dart';

import '../state/model_section.dart';
import '../state/settings_state.dart';
import '../strings.dart';
import '../theme/colors.dart';
import '../theme/motion.dart';
import '../theme/theme.dart';
import 'controls.dart';
import 'markdown_view.dart' show CodeBlock;
import 'settings_form.dart';

/// Un número de `limits` como lo escribe la maqueta: `−2`, `1 000 000`.
String _number(num n) {
  final neg = n < 0;
  final v = n.abs();
  String text;
  if (v == v.roundToDouble()) {
    final digits = v.round().toString();
    final b = StringBuffer();
    for (var i = 0; i < digits.length; i++) {
      if (i > 0 && (digits.length - i) % 3 == 0 && digits.length > 4) {
        b.write(' ');
      }
      b.write(digits[i]);
    }
    text = b.toString();
  } else {
    text = '$v';
  }
  return neg ? '−$text' : text;
}

/// Configuración del modelo (maqueta `Config-modelo`).
class ModelPanel extends StatefulWidget {
  final ModelSection section;
  const ModelPanel(this.section, {super.key});

  @override
  State<ModelPanel> createState() => _ModelPanelState();
}

class _ModelPanelState extends State<ModelPanel> {
  bool _advanced = false;

  ModelSection get m => widget.section;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      spacing: 24,
      children: [
        _prompt(context),
        if (m.noModel case final text?)
          _noModel(context, text)
        else ...[
          _params(context),
          _advancedPart(context),
        ],
      ],
    );
  }

  Widget _options<T>(
    BuildContext context, {
    required String label,
    required List<(T, String)> options,
    required T value,
    required ValueChanged<T> onChanged,
  }) => Semantics(
    label: label,
    container: true,
    explicitChildNodes: true,
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      spacing: 8,
      children: [
        Text(label, style: context.styles.label),
        Wrap(
          spacing: 4,
          runSpacing: 4,
          children: [
            for (final (v, text) in options)
              Semantics(
                inMutuallyExclusiveGroup: true,
                checked: v == value,
                child: Pressable(
                  onPressed: () => onChanged(v),
                  selected: v == value,
                  minHeight: 44,
                  padding: const EdgeInsets.symmetric(horizontal: 10),
                  child: Row(
                    mainAxisSize: MainAxisSize.min,
                    spacing: 10,
                    children: [
                      RadioMark(selected: v == value),
                      Text(text, style: context.styles.row),
                    ],
                  ),
                ),
              ),
          ],
        ),
      ],
    ),
  );

  Widget _prompt(BuildContext context) {
    final s = context.styles;
    final c = context.colors;
    final custom = m.mode == PromptMode.custom;
    return SettingsCard(
      title: Strings.systemPrompt,
      trailing: Text(Strings.forAllModels, style: s.note),
      children: [
        _options<PromptMode>(
          context,
          label: Strings.promptMode,
          value: m.mode,
          onChanged: m.setMode,
          options: const [
            (PromptMode.defaults, Strings.promptDefault),
            (PromptMode.none, Strings.promptNone),
            (PromptMode.custom, Strings.promptCustom),
          ],
        ),
        if (custom)
          Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            spacing: 8,
            children: [
              Text(Strings.promptText, style: s.label),
              // Crece hasta 10 líneas y después se desplaza solo.
              FieldBox(
                controller: m.prompt,
                label: Strings.promptText,
                minLines: 5,
                maxLines: 10,
                invalid: m.promptError != null,
                onChanged: (_) => m.promptEdited(),
              ),
            ],
          ),
        Wrap(
          alignment: WrapAlignment.spaceBetween,
          crossAxisAlignment: WrapCrossAlignment.center,
          spacing: 12,
          runSpacing: 8,
          children: [
            Text(
              custom && m.bytes != null
                  ? Strings.bytesCounter(_number(m.bytes!), _number(m.maxBytes))
                  : '',
              style: s.noteMono,
            ),
            Pressable(
              onPressed: m.mode == PromptMode.defaults ? null : m.restorePrompt,
              borderColor: c.border,
              minHeight: 40,
              padding: const EdgeInsets.symmetric(horizontal: 14),
              child: Center(
                widthFactor: 1,
                child: Text(
                  Strings.restorePrompt,
                  style: m.mode == PromptMode.defaults
                      ? s.row.copyWith(color: c.textMuted)
                      : s.row,
                ),
              ),
            ),
          ],
        ),
        FieldMessage(m.promptError),
        if (m.promptWarning case final w?) FieldMessage(w, error: false),
      ],
    );
  }

  Widget _noModel(BuildContext context, String text) {
    final s = context.styles;
    final c = context.colors;
    return SettingsCard(
      title: Strings.modelParams,
      children: [
        Text(text, style: s.row),
        Align(
          alignment: Alignment.centerLeft,
          child: Pressable(
            onPressed: () => m.owner?.show(SettingsPage.provider),
            color: c.accent,
            hoverColor: c.accent,
            minHeight: 44,
            padding: const EdgeInsets.symmetric(horizontal: 16),
            child: Center(
              widthFactor: 1,
              child: Text(
                Strings.goToProvider,
                style: s.primaryButton.copyWith(color: c.textOnAccent),
              ),
            ),
          ),
        ),
      ],
    );
  }

  Widget _params(BuildContext context) {
    final s = context.styles;
    final c = context.colors;
    return SettingsCard(
      title: Strings.paramsOf(m.model),
      subtitle: Strings.paramsNote(m.providerName),
      trailing: Pressable(
        onPressed: m.restoreAll,
        borderColor: c.border,
        minHeight: 40,
        padding: const EdgeInsets.symmetric(horizontal: 14),
        child: Center(
          widthFactor: 1,
          child: Text(Strings.restoreAll, style: s.row),
        ),
      ),
      children: [
        Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          spacing: 8,
          children: [
            _options<String?>(
              context,
              label: Strings.reasoning,
              value: m.reasoning,
              onChanged: m.setReasoning,
              options: const [
                (null, Strings.reasoningDefault),
                ('off', Strings.reasoningOff),
                ('low', Strings.reasoningLow),
                ('medium', Strings.reasoningMedium),
                ('high', Strings.reasoningHigh),
              ],
            ),
            if (m.reasoningHelp.isNotEmpty)
              Text(m.reasoningHelp, style: s.note),
          ],
        ),
        LayoutBuilder(
          builder: (context, box) {
            const gap = 16.0;
            final columns = box.maxWidth >= 480 ? 2 : 1;
            final width = (box.maxWidth - gap * (columns - 1)) / columns;
            return Wrap(
              spacing: gap,
              runSpacing: 16,
              children: [
                for (final k in kNumberFields)
                  SizedBox(width: width, child: _numberField(context, k)),
              ],
            );
          },
        ),
        Text(m.topKNote, style: s.note),
        FieldMessage(m.paramsError),
      ],
    );
  }

  Widget _numberField(BuildContext context, String key) {
    final s = context.styles;
    final label = Strings.paramLabel(key);
    final error = m.fieldErrors[key];
    final limit = m.limits[key];
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      spacing: 6,
      children: [
        Text(label, style: s.label),
        FieldBox(
          controller: m.fields[key]!,
          label: label,
          mono: true,
          hint: Strings.paramDefault,
          invalid: error != null,
          onChanged: (_) => m.paramsEdited(),
        ),
        if (error != null)
          FieldMessage(error)
        else if (limit != null)
          Text(
            Strings.range(
              _number(limit.min),
              _number(limit.max),
              exclusive: limit.minExclusive,
            ),
            style: s.note,
          ),
      ],
    );
  }

  Widget _advancedPart(BuildContext context) {
    final s = context.styles;
    final c = context.colors;
    final motion = Motion.of(context);
    return Container(
      decoration: BoxDecoration(
        color: c.sidebar,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: c.border),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Semantics(
            expanded: _advanced,
            child: Pressable(
              onPressed: () => setState(() => _advanced = !_advanced),
              radius: 16,
              minHeight: 56,
              padding: const EdgeInsets.symmetric(horizontal: 24),
              child: Row(
                spacing: 10,
                children: [
                  AnimatedRotation(
                    turns: _advanced ? 0.25 : 0,
                    duration: motion.fold,
                    curve: Motion.enter,
                    child: Icon(
                      Icons.chevron_right_rounded,
                      size: 18,
                      color: c.textMuted,
                    ),
                  ),
                  Text(Strings.advanced, style: s.cardTitle),
                  Flexible(
                    child: Text(
                      Strings.advancedNote,
                      style: s.note,
                      overflow: TextOverflow.ellipsis,
                    ),
                  ),
                ],
              ),
            ),
          ),
          AnimatedSize(
            duration: motion.fold,
            curve: Motion.enter,
            alignment: Alignment.topCenter,
            child: !_advanced
                ? const SizedBox(width: double.infinity)
                : Padding(
                    padding: const EdgeInsets.fromLTRB(24, 0, 24, 24),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      spacing: 16,
                      children: [
                        Column(
                          crossAxisAlignment: CrossAxisAlignment.stretch,
                          spacing: 8,
                          children: [
                            Text(Strings.extraJson, style: s.label),
                            FieldBox(
                              controller: m.extra,
                              label: Strings.extraJson,
                              mono: true,
                              minLines: 4,
                              maxLines: 12,
                              invalid: m.extraError != null,
                              onChanged: (_) => m.paramsEdited(),
                            ),
                            Text(m.extraNote, style: s.note),
                            FieldMessage(m.extraError),
                          ],
                        ),
                        if (m.preview case final preview?)
                          Semantics(
                            label: Strings.previewLabel,
                            child: CodeBlock(
                              language: Strings.preview,
                              code: preview,
                              maxHeight: 320,
                            ),
                          ),
                      ],
                    ),
                  ),
          ),
        ],
      ),
    );
  }
}
