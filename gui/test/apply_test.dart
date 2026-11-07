import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/models/conversation.dart';
import 'package:moss_gui/models/events.dart';
import 'package:moss_gui/rpc/client.dart';
import 'package:moss_gui/state/apply.dart';

import 'fake_server.dart';

/// El evento [method] tal como lo manda el servidor.
ConversationEvent ev(String method, Map<String, Object?> p) =>
    RpcEvent.parse(method, {'conversation': 'c1', ...p}) as ConversationEvent;

ConversationSnapshot run(ConversationSnapshot s, List<ConversationEvent> es) {
  for (final e in es) {
    s = applyEvent(s, e)!;
  }
  return s;
}

const empty = ConversationSnapshot(conversation: 'c1', rev: 0);

void main() {
  group('aplicar eventos', () {
    test('agregar', () {
      final s = applyEvent(
        empty,
        ev('conversation.entry_added', {
          'rev': 1,
          'index': 0,
          'entry': entry('user', text: 'hola'),
        }),
      )!;
      expect(s.rev, 1);
      expect(s.entries.single.kind, EntryKind.user);
      expect(s.entries.single.text, 'hola');
    });

    test('texto y pensamiento sobre la última', () {
      final s = run(empty, [
        ev('conversation.entry_added', {
          'rev': 1,
          'index': 0,
          'entry': entry('assistant', reasoning: 'Pien', inProgress: true),
        }),
        ev('conversation.reasoning_delta', {
          'rev': 2,
          'index': 0,
          'text': 'so.',
        }),
        ev('conversation.entry_delta', {'rev': 3, 'index': 0, 'text': '¡Ho'}),
        ev('conversation.entry_delta', {'rev': 4, 'index': 0, 'text': 'la!'}),
      ]);
      expect(s.entries.single.reasoning, 'Pienso.');
      expect(s.entries.single.text, '¡Hola!');
      expect(s.entries.single.inProgress, isTrue);
    });

    test('marcas', () {
      final s = run(empty, [
        ev('conversation.entry_added', {
          'rev': 1,
          'index': 0,
          'entry': entry('assistant', text: 'a', inProgress: true),
        }),
        ev('conversation.entry_updated', {
          'rev': 2,
          'index': 0,
          'entry': entry('assistant', text: 'a', cancelled: true, note: 'n'),
        }),
      ]);
      final e = s.entries.single;
      expect((e.inProgress, e.cancelled, e.note), (false, true, 'n'));
    });

    test('fase, guardado y renombrado', () {
      final s = run(empty, [
        ev('conversation.phase', {'rev': 1, 'phase': 'thinking'}),
        ev('conversation.saved', {'rev': 2, 'id': 'x', 'title': 'Hola'}),
        ev('conversation.renamed', {'rev': 3, 'title': 'Otro'}),
      ]);
      expect((s.phase, s.id, s.title, s.rev), (Phase.thinking, 'x', 'Otro', 3));
    });

    test('hueco de rev → conversation.get (null)', () {
      expect(
        applyEvent(
          empty,
          ev('conversation.phase', {'rev': 2, 'phase': 'thinking'}),
        ),
        isNull,
      );
    });

    test('índice que no cuadra → conversation.get (null)', () {
      expect(
        applyEvent(
          empty,
          ev('conversation.entry_delta', {'rev': 1, 'index': 3, 'text': 'x'}),
        ),
        isNull,
      );
    });

    test('evento viejo o de otra conversación: sin cambios', () {
      final s = empty.copyWith(rev: 5);
      final old = ev('conversation.phase', {'rev': 5, 'phase': 'thinking'});
      final other = RpcEvent.parse('conversation.phase', {
        'conversation': 'c2',
        'rev': 6,
        'phase': 'thinking',
      });
      expect(applyEvent(s, old), same(s));
      expect(applyEvent(s, other as ConversationEvent), same(s));
    });

    // PROTOCOL.md §7, "Respuesta con pensamiento".
    test('secuencia con pensamiento', () {
      final s = run(empty, [
        ev('conversation.entry_added', {
          'rev': 1,
          'index': 0,
          'entry': entry('user', text: 'hola'),
        }),
        ev('conversation.phase', {'rev': 2, 'phase': 'thinking'}),
        ev('conversation.entry_added', {
          'rev': 3,
          'index': 1,
          'entry': entry('assistant', reasoning: 'Me ', inProgress: true),
        }),
        ev('conversation.reasoning_delta', {
          'rev': 4,
          'index': 1,
          'text': 'saludan.',
        }),
        ev('conversation.entry_delta', {'rev': 5, 'index': 1, 'text': '¡Hola'}),
        ev('conversation.phase', {'rev': 6, 'phase': 'streaming'}),
        ev('conversation.entry_delta', {'rev': 7, 'index': 1, 'text': '!'}),
        ev('conversation.entry_updated', {
          'rev': 8,
          'index': 1,
          'entry': entry('assistant', text: '¡Hola!', reasoning: 'Me saludan.'),
        }),
        ev('conversation.saved', {'rev': 9, 'id': 'a1', 'title': 'hola'}),
        ev('conversation.phase', {'rev': 10, 'phase': 'idle'}),
        ev('chat.finished', {'rev': 11, 'outcome': 'ok', 'restore_text': null}),
      ]);
      expect(s.rev, 11);
      expect(s.phase, Phase.idle);
      expect(s.id, 'a1');
      expect(s.entries.map((e) => e.kind), [
        EntryKind.user,
        EntryKind.assistant,
      ]);
      expect(s.entries[1].text, '¡Hola!');
      expect(s.entries[1].reasoning, 'Me saludan.');
      expect(s.entries[1].inProgress, isFalse);
    });

    // PROTOCOL.md §7, "Respuesta con búsqueda".
    test('secuencia con búsqueda', () {
      final s = run(empty, [
        ev('conversation.entry_added', {
          'rev': 1,
          'index': 0,
          'entry': entry('user', text: '/buscar linux'),
        }),
        ev('conversation.phase', {'rev': 2, 'phase': 'searching'}),
        ev('conversation.phase', {'rev': 3, 'phase': 'thinking'}),
        ev('conversation.entry_added', {
          'rev': 4,
          'index': 1,
          'entry': entry('assistant', text: 'Según', inProgress: true),
        }),
        ev('conversation.phase', {'rev': 5, 'phase': 'streaming'}),
        ev('conversation.entry_delta', {'rev': 6, 'index': 1, 'text': ' [1]'}),
        ev('conversation.entry_updated', {
          'rev': 7,
          'index': 1,
          'entry': entry('assistant', text: 'Según [1]'),
        }),
        ev('conversation.entry_added', {
          'rev': 8,
          'index': 2,
          'entry': entry(
            'sources',
            sources: [
              {
                'n': 1,
                'title': 'Kernel',
                'url': 'https://kernel.org',
                'published_date': null,
              },
            ],
          ),
        }),
        ev('conversation.saved', {'rev': 9, 'id': 'b2', 'title': 'linux'}),
        ev('conversation.phase', {'rev': 10, 'phase': 'idle'}),
        ev('chat.finished', {'rev': 11, 'outcome': 'ok', 'restore_text': null}),
      ]);
      expect(s.entries.map((e) => e.kind), [
        EntryKind.user,
        EntryKind.assistant,
        EntryKind.sources,
      ]);
      expect(s.entries[2].sources.single.url, 'https://kernel.org');
      expect(s.phase, Phase.idle);
    });
  });

  test('Pensamiento · N palabras, como la TUI', () {
    expect(thoughtSummary('una'), 'Pensamiento · 1 palabra');
    expect(thoughtSummary(' dos\n\tpalabras '), 'Pensamiento · 2 palabras');
    expect(
      thoughtSummary(List.filled(1234, 'x').join(' ')),
      'Pensamiento · 1 234 palabras',
    );
  });

  group('buscar moss-rpc', () {
    test('--rpc gana', () {
      final l = locateRpc(
        ['--rpc=/dev/moss-rpc'],
        '/app/moss-gui',
        windows: false,
        exists: (_) => true,
      );
      expect((l.path, l.found), ('/dev/moss-rpc', true));
    });

    test('junto al ejecutable, sin PATH', () {
      expect(
        locateRpc(
          [],
          '/opt/moss/moss-gui',
          windows: false,
          exists: (_) => true,
        ).path,
        '/opt/moss/moss-rpc',
      );
      expect(
        locateRpc(
          [],
          r'C:\Moss\moss-gui.exe',
          windows: true,
          exists: (_) => true,
        ).path,
        r'C:\Moss\moss-rpc.exe',
      );
    });

    test('no existe', () {
      final l = locateRpc(
        [],
        '/opt/moss/moss-gui',
        windows: false,
        exists: (_) => false,
      );
      expect((l.path, l.found), ('/opt/moss/moss-rpc', false));
    });
  });

  test('la versión del cliente es la de pubspec.yaml', () {
    final pubspec = File('pubspec.yaml').readAsStringSync();
    final version = RegExp(
      r'^version: ([^+\s]+)',
      multiLine: true,
    ).firstMatch(pubspec)!;
    expect(kClientVersion, version.group(1));
  });
}
