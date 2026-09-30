# HPDG Pattern Generation (RU / EN)

This document explains **how HPDG places drum hits** for **BoomBap** and **Trap/Drill**, at a “readme-level” (not a full academic spec), and what math / rules are used.

---

## RU — Как строится генерация

### 1) Общая модель (для всех жанров)

**Сетка**
- HPDG работает на дискретной сетке: **1 такт = 16 шагов** (16th).
- Позиция ноты хранится как:
  - `step = bar * 16 + stepInBar` (целый шаг)
  - `microOffset` (микросдвиг времени в тиках) — для свинга/человечности.

**Детерминированная “случайность”**
- Вариативность делается через `std::mt19937 rng(seed)` и распределения `uniform_real_distribution(0..1)` и т.п.
- Это означает: одинаковые параметры + один `seed` → **одинаковый результат** (повторяемость).

**Параметры, которые реально влияют**
- `bpm`, `bars`, `densityAmount`
- `swingPercent`, `timingAmount`, `humanizeAmount`, `velocityAmount`
- `seed` / `seedLock`
- `tempoInterpretationMode` (`Auto / Original / Half-time Aware`)
- `genre` (BoomBap / Rap / Trap / Drill)

**Слои**
- Kick, Snare/Clap, HiHat/HatFX/OpenHat, Perc, Ride/Cymbal, Sub808 и т.д.
- Каждый слой имеет собственный генератор и свои правила.

**Пост-правила (enforcers)**
- После первичной расстановки могут применяться “ремонты” и ограничения (например: не терять backbeat, не уходить в machine-gun ковёр, и т.д.).

---

### 2) Tempo Mode (Auto) и “half/double-time”

В DAW одно и то же число BPM может ощущаться по-разному в жанрах:
- Trap/Drill часто воспринимается как **double-time** при низком host BPM (например, 70 “как 140”).
- BoomBap часто воспринимается как **half-time** при высоком host BPM (например, 140 “как 70”).

Поэтому в `Tempo Mode = Auto` HPDG делает **жанровую интерпретацию темпа** перед тем как выбрать “темпо-бэнд” (`TempoBand: Base/Elevated/Fast`) и включить нужные ветки поведения.

Упрощённая логика Auto:
- Trap/Drill: если `hostBpm <= ~90`, то используем `bpm' = hostBpm * 2`
- BoomBap: если `hostBpm >= ~120`, то используем `bpm' = hostBpm * 0.5`
- иначе `bpm' = hostBpm`

Дальше уже сравнение `bpm'` с порогами жанра → выбор `TempoBand` → другие правила/плотности/роллы.

Реализация: `Source/Engine/TempoInterpretation.h` (`interpretedBpmForGenre`, `selectTempoBand`).

---

### 3) BoomBap — почему ноты “там”

**(A) “Скелет” жанра (жёсткие опоры)**
- BoomBap почти всегда держит **backbeat**: основная малость на **2 и 4 доле**.
- В терминах 16-step сетки это обычно шаги **4 и 12** в каждом такте (или эквивалентные по бару).
- Поэтому часть попаданий либо:
  - ставится всегда (инвариант), либо
  - имеет очень высокий шанс + “ремонтируется” enforcer’ом.

**(B) Шаблоны + вариации**
- Kick/часть паттерна часто стартует из библиотеки шаблонов: выбирается “template” (набор шагов и ролей), а затем слегка мутируется.
- Это даёт ощущение “музыкальности”: база не чисто случайная, а взята из корпуса шаблонов.

**(C) Вероятности, плотность и роли**
Типовая математика в стиле:
- вычисляем вероятность (или вес) как линейную комбинацию факторов и ограничиваем:
  - `p = clamp(base + w1*f(density) + w2*f(role) + w3*f(seedDrift) + ... , 0..1)`
- затем решение:
  - “ставить ли ноту” = `random(0..1) < p`

**(D) Swing / microtiming**
- Swing и humanize — это в первую очередь **временные смещения** (`microOffset`) и небольшие сдвиги velocity, а не “перестановка шагов”.
- Математически: добавляем смещение, потом `clamp(min..max)`.

Кодовые ориентиры:
- `Source/Engine/BoomBap/BoomBapClassicAlgebraGenerator.cpp`
- `Source/Engine/BoomBap/BoomBapClassicPatternScorer.cpp`
- `Source/Engine/BoomBapEngine.cpp`

---

### 4) Trap / Drill — почему ноты “там”

**(A) Другая геометрия**
- Trap/Drill строится вокруг:
  - снейра/клапа как опоры,
  - hats как носителя “движения” (деления 1/8 → 1/16 → 1/32 → 1/64, роллы/бёрсты),
  - kick’ов с большим числом синкоп и фразовых вставок,
  - отдельной логики для 808.

**(B) TempoBand сильно меняет поведение**
- При Auto интерпретации (например, DAW 70 → “ощущение 140”) Trap/Drill чаще попадает в `TempoBand::Elevated`,
  и генераторы включают “быстрые” ветки (в т.ч. для hats).

**(C) Математика та же по типу, но “агрессивнее”**
- Всё ещё: `p = clamp(…)`, затем `rand < p`, но коэффициенты/ветки сильнее завязаны на “темпо-бэнд”, плотность и подстили.

Кодовые ориентиры:
- `Source/Engine/TrapEngine.cpp`
- `Source/Engine/Trap/TrapAlgebraEngine.cpp`
- `Source/Engine/Trap/TrapQualityScorer.cpp`
- `Source/Engine/DrillEngine.cpp`

---

### 5) Какие “математические законы” используются

Если коротко и честно, это смесь:
1) **Комбинаторика на дискретной сетке** (позиции событий на {0..bars*16-1})
2) **Вероятностные модели** (бернуллиевы решения `rand < p`)
3) **Шаблонные базы + стохастические мутации**
4) **Ограничения / инварианты** (hard rules: backbeat, “не мусорить”)
5) **Нелинейности** уровня `pow`, `fmod`, `clamp`, (и часто простые piecewise-условия)

---

## EN — How generation works

### 1) Shared model (all genres)

**Grid**
- HPDG uses a discrete grid: **1 bar = 16 steps** (16th).
- A note position is represented as:
  - `step = bar * 16 + stepInBar`
  - `microOffset` (sub-step timing offset in ticks) used for swing/humanize.

**Deterministic randomness**
- Variability comes from `std::mt19937 rng(seed)` with `uniform_real_distribution(0..1)` etc.
- Same parameters + same `seed` → **same output**.

**What parameters actually matter**
- `bpm`, `bars`, `densityAmount`
- `swingPercent`, `timingAmount`, `humanizeAmount`, `velocityAmount`
- `seed` / `seedLock`
- `tempoInterpretationMode` (`Auto / Original / Half-time Aware`)
- `genre` (BoomBap / Rap / Trap / Drill)

**Layered generators**
- Kick, Snare/Clap, HiHat/HatFX/OpenHat, Perc, Ride/Cymbal, Sub808, etc.
- Each layer has its own generator and constraints.

**Post rules (enforcers)**
- After the first pass, HPDG may “repair” patterns (keep backbeat, avoid pathological runs, preserve genre semantics, etc.).

---

### 2) Tempo Mode (Auto) and half/double-time

DAW tempo numbers are interpreted differently across genres:
- Trap/Drill is often perceived as **double-time** at low host BPM (e.g. 70 “feels like” 140).
- BoomBap is often perceived as **half-time** at high host BPM (e.g. 140 “feels like” 70).

So in `Tempo Mode = Auto`, HPDG applies **genre-aware tempo folding** before selecting `TempoBand (Base/Elevated/Fast)` and enabling tempo-dependent branches.

Simplified Auto logic:
- Trap/Drill: if `hostBpm <= ~90` then `bpm' = hostBpm * 2`
- BoomBap: if `hostBpm >= ~120` then `bpm' = hostBpm * 0.5`
- else `bpm' = hostBpm`

Implementation: `Source/Engine/TempoInterpretation.h` (`interpretedBpmForGenre`, `selectTempoBand`).

---

### 3) BoomBap — why hits land where they do

**(A) Genre skeleton (hard anchors)**
- BoomBap typically requires a stable **backbeat** (snare on beats 2 and 4).
- On a 16-step grid that commonly maps to steps **4 and 12** per bar.
- HPDG keeps this either as a hard rule, or via very high probabilities + post-repair.

**(B) Templates + variations**
- Kick (and other lanes) often start from a template library, then get probabilistic mutations.
- This is how HPDG stays musical instead of purely random.

**(C) Density, bar roles, probabilities**
Typical math pattern:
- compute a probability/weight as a weighted sum of factors and clamp:
  - `p = clamp(base + w1*f(density) + w2*f(role) + w3*f(seedDrift) + ... , 0..1)`
- sample:
  - “place hit?” = `random(0..1) < p`

**(D) Swing / microtiming**
- Swing/humanize are mainly **timing offsets** (`microOffset`) and velocity tweaks, not step reshuffles.

Code pointers:
- `Source/Engine/BoomBap/BoomBapClassicAlgebraGenerator.cpp`
- `Source/Engine/BoomBap/BoomBapClassicPatternScorer.cpp`
- `Source/Engine/BoomBapEngine.cpp`

---

### 4) Trap / Drill — why hits land where they do

**(A) Different geometry**
- Trap/Drill is built around:
  - snare/clap anchors,
  - hats as the motion carrier (1/8 → 1/16 → 1/32 → 1/64, rolls/bursts),
  - kicks with more syncopation and phrase-level inserts,
  - a separate 808 logic.

**(B) TempoBand drives branching**
- With Auto folding (e.g. DAW 70 → “felt 140”), Trap/Drill often end up in `TempoBand::Elevated`,
  enabling faster hat/kick behaviors.

**(C) Same math type, stronger modulation**
- Still probability + clamp + sampling, but with stronger tempo-band and substyle branching.

Code pointers:
- `Source/Engine/TrapEngine.cpp`
- `Source/Engine/Trap/TrapAlgebraEngine.cpp`
- `Source/Engine/Trap/TrapQualityScorer.cpp`
- `Source/Engine/DrillEngine.cpp`

---

### 5) What “math laws” are actually used

In practice it’s a mix of:
1) **Discrete combinatorics on a grid** (events on {0..bars*16-1})
2) **Probabilistic models** (Bernoulli decisions `rand < p`)
3) **Template bases + stochastic mutations**
4) **Constraints / invariants** (hard rules for genre semantics)
5) Simple nonlinearities like `pow`, `fmod`, and heavy use of `clamp`, plus piecewise conditions

