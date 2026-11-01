# DarkTrap — проверка и примеры

Отчёт: `docs/audit/DARK_TRAP_2026_11.md`.
Параметры для SynthBassVST: `docs/SYNTHBASSVST_DARKTRAP_HANDOFF.md`.

`listening/before` и `listening/after`: одинаковые настройки — 150 BPM с BPM Lock,
C minor, четыре такта, Density .40, Style rolls, seeds 1, 2, 3.
Эти примеры не выбирались по качеству или максимальному score.

`listening/fills`: те же seeds 1–3 и Density .40, режим Fills для явного примера дробей.
`listening/sparse`: те же seeds 1–3, Density .20, Style rolls; seed 3 попадает в вариант
с протяжённым сабом. Это отдельная настройка плотности, не замена обычного DarkTrap.

- `seedNNN_mix.wav`: полный рисунок штатным renderer HPDG.
- `seedNNN_bass.wav`, `seedNNN_kick.wav`: отдельные слои для проверки взаимодействия.
- `seedNNN_bass.mid`: бас с фактическими gate-длительностями.
- `seedNNN_drums_and_bass.mid`: диагностический MIDI по note dump, bass channel 1,
  drums channel 10, PPQ 960. Сохранены положения, velocity и длительности;
  специальные glide-флаги HPDG этот диагностический MIDI не кодирует.

WAV исполняется текущими голосами HPDG. Отдельный SynthBassVST сюда не подключался.
Сравнение ритма не требует совпадения тембра будущего инструмента с этим renderer.

Ручное прослушивание остаётся открытым. Для оценки удобно использовать 0–4:
0 непригодно, 1 требует большой правки, 2 применимо с правками, 3 хорошо, 4 готов использовать.
Автоматические числа не являются оценкой субъективной похожести в процентах.

Сводные результаты — `comparison.json`; исходные большие CSV, executables до/после,
кадры видео, транскрипции и все сторонние аудиофайлы остаются в игнорируемом
`build/darktrap-reference`. Сюда экспортированы только паттерны самого HPDG.
