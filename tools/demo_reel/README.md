# Demo reel — видео возможностей HPDG

Ролик собирается автоматически из настоящего VST3-интерфейса и настоящего звука движка:

1. `make_samples.py` синтезирует два демо-семпла (луп на Rhodes в A minor, 88 BPM, и фанк-брейк,
   96 BPM) — без чужого аудио, Content ID ловить нечего. Пишутся в `media/` (не в git).
2. `HPDG_DemoReel` (`Tests/DemoReel.cpp`) проигрывает сценарий `demo.txt`: жмёт кнопки редактора,
   выбирает жанры и подстили, бросает семплы в Sample Analysis. На каждый кадр — снимок UI
   (30 fps), звук — прямо из `processBlock`, события — в `timeline.jsonl`.
3. `compose.py` рисует поверх курсор, клики, выпадающие меню, перетаскивание файла, подсветку,
   зум, титры и заставки; выравнивает громкость между жанрами, ставит лимитер и кодирует MP4
   1920×1080 (H.264 + AAC).

Всё одной командой:

```
powershell -ExecutionPolicy Bypass -File tools\demo_reel\make_video.ps1
```

Результат: `build\demo_reel\HPDG_demo.mp4`. Быстрая проверка картинки без полного рендера:
`python tools\demo_reel\compose.py build\demo_reel out.mp4 --stills 300,1200,6400`.

Сценарий правится в `demo.txt` — команды описаны в начале `Tests/DemoReel.cpp`
(`click`, `combo`, `knob`, `drop`, `caption`, `chapter`, `title`, `zoom`, `highlight`, `bars`, …).
