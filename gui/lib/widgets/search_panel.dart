import 'package:flutter/material.dart';

import '../state/search_section.dart';
import '../strings.dart';
import '../theme/colors.dart';
import '../theme/theme.dart';
import 'settings_form.dart';

/// Búsqueda web (maqueta `Config-busqueda`).
class SearchPanel extends StatelessWidget {
  final SearchSection section;
  const SearchPanel(this.section, {super.key});

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      spacing: 24,
      children: [
        SettingsCard(
          title: Strings.searchState,
          children: [
            Wrap(
              crossAxisAlignment: WrapCrossAlignment.center,
              spacing: 16,
              runSpacing: 8,
              children: [
                Row(
                  mainAxisSize: MainAxisSize.min,
                  spacing: 8,
                  children: [
                    Container(
                      width: 8,
                      height: 8,
                      decoration: BoxDecoration(
                        color: section.configured ? c.accent : c.textMuted,
                        shape: BoxShape.circle,
                      ),
                    ),
                    Text(
                      section.configured
                          ? Strings.searchConfigured
                          : Strings.searchNotConfigured,
                      style: s.row,
                    ),
                  ],
                ),
                if (section.keyStatus.isNotEmpty)
                  Text(section.keyStatus, style: s.noteMono),
              ],
            ),
          ],
        ),
        SettingsCard(
          title: Strings.searchKeyTitle,
          subtitle: Strings.searchKeyNote,
          children: [
            SecretField(
              label: Strings.newApiKey,
              controller: section.apiKey,
              lockedBy: section.locked ? 'CHAT_SEARCH_API_KEY' : null,
              help: Strings.searchEnvHelp,
              error: section.error,
              onChanged: (_) => section.edited(),
            ),
          ],
        ),
      ],
    );
  }
}
