# Boom Bap Engine и шесть устойчивых субстилей

## Что именно должно оставаться неизменным, чтобы генератор не ломал жанр

В терминах entity["academic_field","Психоакустика","восприятие ритма, тайминга и громкости"] boom bap — это не просто «ударные на 2 и 4», а система, в которой слушатель одновременно получает три вещи: ясную временную опору, достаточный уровень ритмического интереса и приятное телесное ощущение движения. Исследования groove сходятся в том, что лучше всего работают паттерны, где есть устойчивая метрическая сетка и умеренное отклонение от ожидания: умеренная syncopation повышает удовольствие и желание двигаться, а beat salience, event density и fast metrical levels в среднем положительно связаны с groove. Из этого следует главный инженерный принцип: генератор должен сначала строить опору, а уже потом добавлять «сюрприз», а не наоборот. citeturn18view1turn9view1turn9view0turn18view0

Для boom bap это общепсихологическое правило дополняется жанровой спецификой: выраженные kick и snare, разреженная sample-based фактура, chopped breaks, 4-барная репетиция с вариацией и сильно ощущаемый swing. Важна не большая плотность нот, а соотношение между downbeat, backbeat, тянущими offbeat-ами, ghost-слоем и повторяемостью фразы. Именно поэтому boom bap обычно звучит как «кач», даже когда в аранжировке мало элементов: грув рождается не из количества, а из иерархии импульсов и того, как они задерживаются и отвечают друг другу. citeturn6view0turn14view2turn11view1

Исследования microtiming особенно полезны здесь, потому что они отсекают самую частую ошибку генераторов: случайный jitter не равен groove. Реальные и умеренные человеческие отклонения допустимы, но преувеличенный microtiming ухудшает оценки groove; при этом полностью quantized вариант и компетентно исполненный original microtiming могут восприниматься почти одинаково хорошо. Отдельно важно, что ранние сдвиги обычно оцениваются хуже эквивалентно поздних, а ошибки snare наказываются слухом сильнее, чем аналогичные ошибки kick. Инженерный вывод простой: snare системно не торопить; kick держать почти по центру или совсем чуть-чуть ранним; слабые hat-позиции смещать не рандомом, а контролируемым swing. citeturn21view0turn21view2turn8view3

Критически важное следствие для твоего проекта: внутренний grid 1/64 полезен как **структурная** сетка, но он слишком груб для **покета**. Формула длины одного тика такая:  
\[
\text{tick64\_ms}(bpm)=\frac{60000}{16\cdot bpm}.
\]
То есть при привычных для boom bap темпах около 80–100 BPM один шаг 1/64 — это примерно 37.5–46.9 мс; при твоём product-ограничении для RussianUnderground 74–90 BPM это уже примерно 41.7–50.7 мс. Классические JND-оценки для timing в изохронных последовательностях находятся около 6 мс для коротких IOI и около 2.5–5% IOI для более длинных интервалов. Значит, pocket нельзя кодировать одними лишь прыжками по 1/64: нужна отдельная непрерывная или более тонкая сущность `microOffsetMs` либо PPQ-offset поверх 1/64-position. Иначе late-snare, lazy-hat и human drag будут квантоваться слишком грубо и начнут звучать не «живыми», а просто криво. citeturn8view1turn8view3turn5search3turn11view0

## Формальная модель общего движка

Правильная абстракция для boom bap — не «рандомный заполнителель нот», а constrained optimization поверх человеческого prior-а. Паттерн удобно описывать так:
\[
P=(X,V,D,M,R),
\]
где \(X\in\{0,1\}^{L\times N}\) — бинарная матрица onset-ов, \(V\) — velocity, \(D\) — duration, \(M\) — micro-offset в миллисекундах или fine-PPQ, \(R\) — роль события (`anchor`, `support`, `ghost`, `pickup`, `turnaround`, `accent`). При 11 фиксированных lanes и 4 барах по 64 тика получается \(N=256\) структурных позиций. Самый важный момент: \(X\) живёт на 1/64-grid, а \(M\) живёт **вне** этого grid и отвечает за pocket. Это соответствует и психоакустике timing, и практике человеческого drumming, и boom-bap эстетике swung/late placement. citeturn8view1turn8view3turn6view0

Оптимизационную задачу лучше задавать так:
\[
P_s^*=\arg\max_{P\in\Omega_{bb}}
\Big[\lambda_{\text{prior}}\log p_{\text{break}}(P)+Q_s(P)-\Psi(P)\Big].
\]
Здесь \(\Omega_{bb}\) — множество hard-constraint boom-bap паттернов, \(p_{\text{break}}(P)\) — prior из человеческих break-like масок и velocity/timing contour-ов, \(Q_s(P)\) — score для выбранного субстиля \(s\), а \(\Psi(P)\) — штрафы за жанровые утечки и мусор. Такой вид модели лучше прямого rule engine, потому что boom bap исторически и технологически связан с chopped drums, swung programming и sample-derived phrasing, а современные expressive drum datasets показывают, что именно dynamics и timing повышают воспринимаемое качество генерации. citeturn6view0turn20search8turn20search11turn20search3

Функцию качества удобно описать через вектор признаков:
\[
Q_s(P)=w_s^\top \phi(P),
\]
где
\[
\phi(P)=
[\text{BackbeatClarity},\ \text{SyncBand},\ \text{HatLife},\ \text{GhostField},\ \text{MicroPlausibility},\ \text{NegativeSpace},\ \text{PhraseVariation},\ \text{BreakResemblance},\ \text{LowEndDiscipline},\ \text{PolishOrRawness}].
\]
Идея здесь такая: все шесть подстилей используют один и тот же общий жанровый каркас, но отличаются весами \(w_s\) и style-specific target bands. Это важнее, чем писать шесть отдельных генераторов, потому что genre-lock должен оставаться единым, а различаться должны лишь профильные коэффициенты, priors и допустимые траектории вариации. citeturn18view1turn18view0turn9view1

Связь swing-параметра с реальным временем тоже нужно хранить явно. Если \(r\) — доля weak 16th внутри пары шестнадцатых, то задержка слабой шестнадцатой относительно straight-grid равна:
\[
\Delta_{\text{swing16}}(bpm,r)=\frac{60000}{bpm}\cdot\frac{2r-1}{4}.
\]
Это означает, что при \(r=0.60\) и темпе 85 BPM weak-16th смещается примерно на 35 мс — то есть swing сам по себе является **структурным** временем, а не «микродрожанием». Поэтому логика должна быть двухслойной: сначала построить swung lattice, потом применить microOffset к отдельным lanes и метрическим классам (`downbeat`, `weak16`, `preSnare`, `postSnare`, `ghost`, `turnaround`). citeturn11view0turn11view3turn8view3

## Общий алгоритм Boom Bap Engine

Hard constraints, которые нельзя ослаблять ни для одного из шести профилей, должны быть такими. Во-первых, snare-backbeat на локальных тиках 16 и 48 в каждом баре обязателен; допускаются фламы, ghost или side-clap рядом с ним, но не замена backbeat-а. Во-вторых, hats не должны переходить в trap-like running rolls: 1/32 и 1/64 допустимы только как ornament, pickup или turnaround, но не как базовая фактура. В-третьих, Sub808 в boom bap — это опциональный low-end reinforcement, а не главный rhythmic narrator; его occupancy должна быть маленькой и короткой. В-четвёртых, open hat, ride, cymbal и perc не должны строить отдельный жанр; они лишь подчёркивают pocket. В-пятых, bar-to-bar variation должна быть измеримой, но не разрушать loop identity: слушатель должен узнавать тот же грув после 1-го бара. citeturn6view0turn11view1turn18view0turn21view0

Практически это лучше всего реализовать не прямым sampling из Bernoulli на каждом тике, а staged generator-ом:

```cpp
Pattern generateBoomBap(seed, profile, bars=4) {
    Pattern best;
    float bestScore = -INF;

    for (int c = 0; c < candidateCount(profile); ++c) {
        Pattern p = makeEmpty();

        p = buildBackbeatSkeleton(p, profile);      // SN on 16/48, bar roles
        p = applyBreakPrior(p, profile, seed, c);   // break-like masks / n-gram prior
        p = generateKickRhetoric(p, profile);       // anchors, pickups, answers
        p = generateHatField(p, profile);           // base 8ths/16ths + dropout mask
        p = generateGhostField(p, profile);         // snare halo, kick ghosts, flams
        p = generateTurnarounds(p, profile);        // bar4 / phrase-end logic
        p = maybeReinforceSub(p, profile);          // short low-end reinforcement only
        p = applySwingLattice(p, profile);          // structural swing
        p = applyMicroOffsets(p, profile);          // post-grid pocket
        p = applyVelocityContours(p, profile);      // dynamics as first-class data
        p = validateAndRepair(p, profile);          // genre lock
        float q = score(p, profile);

        if (q > bestScore) { best = p; bestScore = q; }
    }
    return best;
}
```

Такой pipeline нужен потому, что groove studies и production practice одновременно говорят одно и то же: повторяемая временна́я опора должна появляться первой, а variation должна наслаиваться на уже понятный pulse. Для boom bap особенно важна 4-барная логика `statement → repeat → slight answer → turnaround`, потому что жанр живёт на loop memory, а не на непрерывной новизне. Именно это даёт head-nod: мозг быстро схватывает pattern, а потом получает маленькие отклонения, которые не рушат метр. citeturn18view1turn9view1turn6view0

Kick в boom bap лучше генерировать как «риторическую» сущность, а не как плотность. Удобно разбить бар на окна: `downbeat window`, `pre-snare window`, `mid-bar answer`, `bar-end turnaround`. Внутри каждого окна профиль решает, нужен ли удар, но общая логика остаётся общей: kick либо подтверждает метр, либо толкает в snare, либо отвечает после snare. Ghost kick — это не второй main kick, а тихое заполнение pocket around anchors. Поэтому полезно хранить state machine или маленькую факторную грамматику, а не независимые вероятности на каждом тике. В ear-терминах это и есть разница между «битмейкерским» kick и «MIDI-спамом». citeturn11view1turn18view0turn6view0

Hat engine должен быть устроен как `base template + dropout mask + accent mask + micro pocket`, а не как равномерное заполнение 1/16. Исследования groove показывают, что fast metrical levels и event density действительно помогают, но их вклад быстро падает, если temporal uncertainty или чрезмерный microtiming начинают размывать pulse. Поэтому лучший оператор для boom bap — не «добавить больше hat-нот», а «удалить правильные hat-ноты, сместить слабые hat-позиции и дать velocity contour». Хороший hat engine в этом жанре всегда хранит silence как такой же важный объект, как note. citeturn18view0turn8view3turn9view1

## Переопределение шести product-профилей

Текущий dropdown уже задаёт шесть продуктовых профилей: `Classic`, `Dusty`, `Jazzy`, `BoomBapGold`, `RussianUnderground`, `LofiRap`. Их нужно трактовать не как шесть отдельных жанров, а как шесть weight vectors над одним и тем же boom-bap core. В качестве эстетических полюсов полезно мыслить `Classic` ближе к школам entity["musical_artist","DJ Premier","американский хип-хоп-продюсер"] и entity["musical_artist","Pete Rock","американский хип-хоп-продюсер"], а свободные off-grid допуски `Dusty` и части `Jazzy` — как допускающие больше позднего и неровного pocket, который позднее радикализировал entity["musical_artist","J Dilla","американский хип-хоп-продюсер"]. Это не означает копирование конкретных авторов; это просто удобные полюса для внутреннего style design. citeturn6view0turn11view2turn14view1

**Classic.** Это baseline, от которого остальные профили отклоняются. Целевые диапазоны: `tempo 84–94`, `swing16 0.58–0.61`, `HH = 8ths + 10–25% swung 16th insertions`, `kick = 2–4 main hits/bar`, `ghost snare = low–medium`, `ride ≈ off`, `snareLate = +6..+12 ms`, `kickJitter = -3..+4 ms`, `bar variation = moderate`. По слуху Classic должен давать читаемый head-nod without clutter: жёсткий backbeat, понятный kick rhetoric, чуть ленивые hats, минимум ornament, максимум опоры. Если Classic уже реализован, его надо оставить эталонным профилем и использовать как reference profile для тестов style confusion. citeturn11view1turn11view0turn6view0

**Dusty.** Этот профиль не должен быть «Classic + vinyl noise». Его отличие в break-memory: больше mask-asymmetry, больше dropout-ов, больше поздних hat-позиций, более заметный ghost-field около snare и меньшая стерильность повторов. Целевые диапазоны: `tempo 78–92`, `swing16 0.59–0.63`, `HH 8ths + редкие 16ths`, `dropout high`, `ghost snare = medium–high`, `snareLate = +8..+16 ms`, `breakPrior weight high`, `polish low`, `rawness high`. На слух Dusty должен ощущаться как выжатый из chopped break, даже если он полностью синтезирован из one-shot-ов; он обязан быть punchy, но не clean. Отличие от LofiRap здесь принципиальное: Dusty всё ещё бьёт, просто в нём больше пыли и меньше симметрии. citeturn6view0turn15view0turn14view2

**Jazzy.** Здесь главное отличие должно быть не только в sample choice, а в ритмической грамматике. Целевые диапазоны: `tempo 86–98`, `swing16 0.60–0.64`, `kick density = medium with conversational syncopation`, `ghost snare = high`, `ride usage = low–medium but audible`, `openHat rate = slightly above Classic`, `triplet/grace allowance = yes`, `micro spread wider than Classic`. На слух Jazzy должен производить впечатление более «разговаривающего» грува: hats и ride не метрономят, а дышат; kick и ghost отвечают фразе; вариативность баров выше, чем у Classic. Это хорошо согласуется с тем, что jazz-hop традиционно сочетает hip-hop swing, меньшее доверие к жёсткой quantization и более «атмосферные» timbral cues вроде brushed snare и upright-bass-like low end. citeturn14view1turn14view0turn21view0

**BoomBapGold.** Этот enum лучше переопределить как `golden-era polished / soulful boom bap`, а не как «ещё один classic». Если переименование невозможно, семантику всё равно нужно поменять именно так. Целевые диапазоны: `tempo 88–98`, `swing16 0.55–0.59`, `kick = clearer anchors, fewer broken pickups`, `ghost snare = low`, `bar repetition stronger`, `turnaround cleaner`, `Sub808 reinforcement = short and secondary`, `polish high`, `rawness medium-low`. На слух BoomBapGold должен быть шире, чище и hook-friendly: меньше «пьяной» кривизны, больше front-facing snare, чуть более крупный low-end и меньше мелкой рваности. Если Dusty — это crate dust, то BoomBapGold — это golden-era authority с soul-first логикой. citeturn6view0turn15view0turn14view2

**RussianUnderground.** Этот профиль следует зафиксировать как отдельную product-семантику по твоему ТЗ: `tempo 74–90` и не выше. Но главное здесь не только темп, а характер ритмической рваности. Целевые диапазоны: `swing16 0.59–0.65`, `HH density = low–medium`, `hat dropout = high`, `kick clusters = medium with pre-bar and pre-snare bias`, `ghost snare = medium–high`, `snareLate = +8..+18 ms`, `ride ≈ off`, `openHat rare`, `brokenness high`, `turnaround asymmetry high`. На слух RussianUnderground должен быть медленным, тяжёлым, тянущим вниз и иногда нарочно «рваным»: не trap-half-time, а именно медленный dragging boom bap. Кодово это значит, что profile имеет право чаще удалять ожидаемый weak hat, вставлять короткие pickup kicks перед барлайном, задерживать clap-ghost вокруг backbeat и делать bar2/bar4 заметно «грязнее», чем Classic. Но две вещи остаются неприкосновенными: snare на 2/4 и читаемая метрическая опора. Опора не исчезает; она просто становится более угрюмой и тянущей. citeturn18view0turn21view2turn6view0

**LofiRap.** Этот профиль нельзя делать копией Dusty с пониженной громкостью. Его сущность — мягкость атаки, упрощённый ритмический каркас и более высокий процент повтора. Целевые диапазоны: `tempo 72–88`, `swing16 0.56–0.60`, `HH mostly 8ths`, `kick = 1–3 main hits/bar`, `ghost snare = low`, `cymbal/openHat ≈ minimal`, `bar variation = low–medium`, `softness high`, `velocity peaks lower than other profiles`. На слух LofiRap должен быть спокойнее, круглее и менее конфликтным, но всё ещё boom bap: snare остаётся backbeat-центром, hats сохраняют swing, groove не распадается на «study loop». Такой профиль хорошо поддерживается lo-fi/jazz-hop практиками, где tape/vinyl coloration, re-pitched samples и deliberately aged timbre важны не меньше, чем собственно ноты; инженерно это означает менее агрессивные velocity contours и меньший use of turnaround clutter. citeturn14view1turn10search3turn18view1

## Валидация, repair и объективные метрики

Чтобы генератор «всегда звучал», ему нужен не только generator, но и evaluator. Минимальный набор метрик лучше задать так:
\[
Q_s(P)=w_s^\top\phi(P)-\lambda_s\Psi(P),
\]
где \(\phi(P)\) измеряет полезные свойства, а \(\Psi(P)\) — жанровые аварии. В \(\phi(P)\) должны входить: `BackbeatCoverage`, `BeatSalience`, `SyncopationBand`, `HatLife`, `MicroPlausibility`, `NegativeSpace`, `PhraseVariation`, `BreakResemblance`, `VelocityLife`. В \(\Psi(P)\) должны входить: `TrapLeakPenalty`, `FlatVelocityPenalty`, `EarlySnarePenalty`, `OverhumanizePenalty`, `ClutterPenalty`, `Sub808OverusePenalty`. Практический смысл такой: хороший boom bap — это не максимум complexity, а pattern, в котором слушатель быстро извлекает pulse, получает умеренное нарушение ожидания и не слышит machine-gun или chaotic jitter. Именно этот баланс и следует считать, а не только note count. citeturn9view1turn18view0turn8view3turn21view2

Для `SyncopationBand` удобно использовать velocity-aware syncopation measure типа SG-подхода, где слабые метрические позиции на быстрых уровнях получают больший вес, чем сильные позиции, а amplitude differences учитываются явно. Тогда target можно задавать не абсолютной цифрой «синкопации вообще», а диапазоном на стиль: Classic и BoomBapGold держать в mid-band; Dusty и RussianUnderground — чуть выше и неровнее; Jazzy — самый широкий band; LofiRap — самый спокойный. Для `MicroPlausibility` используйте не «сколько ноты съехали», а расстояние до profile-specific priors по lane и метрическому классу. Иначе система начнёт награждать просто большие отклонения, хотя исследования показывают обратное: exaggerated microtiming вреден, а quantized и умеренно expressive варианты часто оказываются одинаково приемлемыми. citeturn18view0turn21view0turn8view3

Отдельно нужен `TrapLeakPenalty`. Он должен резко расти, если генератор начинает вести себя по trap-логике: длинные серии мелких hat-делений, чрезмерная lane occupancy, sustained 808 как основной narrating layer, слишком частые акцентные hats, ровный machine-gun на hi-hat lane. В boombap допустимы grace hits, little drags, flams и редкие ornamental 1/32/1/64 события, но недопустимы trap-like rolls как база. Этот штраф должен быть именно hard genre wall, а не мягкой рекомендацией, иначе стили постепенно «утекут» к общему hip-hop generator-у и перестанут слышимо различаться. citeturn6view0turn11view1turn18view1

Для human priors и sanity-check не нужно изобретать данные с нуля. Практический стек калибровки такой: urlGroove MIDI Datasetturn20search11 для tempo-aligned expressive drumming; urlExpanded Groove MIDI Datasetturn20search3 для velocity-aware priors и perceptual emphasis на dynamics; urlENST-Drumsturn19view0 для fully annotated acoustic drum sanity и onset robustness; urlSlakh2100turn12search6 для многодорожечной MIDI-aligned проверки и общей устойчивости пайплайна. Но эти датасеты не boombap-labeled, поэтому из них надо брать **человеческий timing/velocity prior**, а не готовую жанровую истину; для стиль-специфики всё равно нужен маленький внутренний reference bank лицензированных boom-bap loops и carefully transcribed examples. citeturn20search11turn20search8turn19view0turn12search6

## ТЗ для реализации в коде

Реализация должна появиться как **один** `BoomBapEngine`, внутри которого живут шесть `BoomBapProfile`, а не как шесть автономных random generator-ов. Видимый snap в UI можно оставить `1/16`, как сейчас, но внутренний timing обязан стать двухслойным: `tick64` для структурной дискретизации и `microOffsetMs` для pocket. Lane order ломать нельзя: `HiHat, HatAccent, OpenHat, Snare, ClapGhost, Kick, KickGhost, Ride, Cymbal, Perc, Sub808`. Output note должен содержать не только `lane/tick/velocity`, но и `role`, `microOffsetMs`, `phraseTag`, `styleConfidence`, а по возможности и `textureHints`, чтобы sample layer позднее мог выбрать более уместную семью сэмплов. Это соответствует как groove literature, так и boom-bap production logic, где swing, dynamics и tactile response не являются украшением — они являются частью сигнала. citeturn6view0turn20search8turn18view1

```cpp
enum class BoomBapSubstyle {
    Classic,
    Dusty,
    Jazzy,
    BoomBapGold,
    RussianUnderground,
    LofiRap
};

struct BoomBapEvent {
    uint8_t lane;
    uint8_t bar;
    uint8_t tick64;
    uint8_t len64;
    uint8_t velocity;
    float   microOffsetMs;
    EventRole role;          // anchor, support, ghost, pickup, turnaround, accent
};

struct BoomBapProfile {
    Range<float> tempoBpm;
    Range<float> swing16;
    Range<float> snareLateMs;
    Range<float> hatWeakLateMs;
    Range<float> kickJitterMs;
    Range<float> kickPerBar;
    Range<float> ghostSnareRate;
    Range<float> hat16InsertRate;
    Range<float> hatDropoutRate;
    Range<float> openHatPer4Bars;
    Range<float> ridePer4Bars;
    Range<float> subReinforceRate;
    Range<float> syncTarget;
    Range<float> variationTarget;
    float breakPriorWeight;
    float rawness;
    float polish;
    float brokenness;
    float softness;
};
```

Архитектурно нужны модули `BoomBapBreakPrior`, `BoomBapScorer`, `BoomBapValidator`, `BoomBapRepair`, `BoomBapMicroPocketModel` и `BoomBapProfileLibrary`. `BreakPrior` даёт human-like bar masks и contour priors; `Scorer` считает \(Q_s(P)\); `Validator` проверяет hard locks; `Repair` добавляет missing backbeat, убирает trap leakage, ослабляет clutter, чинит early snare и flat velocity; `MicroPocketModel` назначает style-aware offsets по lane и metric class; `ProfileLibrary` хранит шесть product-профилей. Candidate selection обязателен: один паттерн нельзя принимать на первом проходе. Нужны хотя бы 24–64 кандидата на генерацию, после чего выбирается лучший по \(Q_s\). Same seed должен давать same pattern, а разные seeds внутри одного профиля — различный, но всё ещё узнаваемо профильный материал. citeturn18view1turn9view1turn21view0

Критерии приёмки должны быть жёсткими. Для каждого профиля: backbeat в каждом баре есть; snare не системно уходит в early; hats не machine-gun; microOffset хранится отдельно от tick64; generated loop проходит `TrapLeakPenalty == 0`; style classifier различает шесть профилей лучше случайного уровня с большим запасом; blind listening даёт стабильное распознавание Classic/Dusty/Jazzy/Gold/RussianUnderground/LofiRap; same-seed детерминирован; bar 4 даёт turnaround, но не ломает loop identity. Если коротко, движок должен строить не «случайные drums», а **предсказуемую опору плюс дозированное нарушение ожидания**. Именно это и заставляет boom bap качать: сильный backbeat, умеренная syncopation, живая hat-артикуляция, поздний snare вместо rushed snare, и повтор такой длины, чтобы мозг успел выучить грув, но не успел заскучать. citeturn9view1turn18view0turn21view2turn21view0turn6view0