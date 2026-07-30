# gr2 скелетная анимация (3dmob) — план реализации

Статичный рендер + текстуры gr2-моделей уже готовы (Gr2.cpp / Gr2Models.cpp). Следующий заход —
скелетная анимация: стражи/дракон/хугелинг должны ходить/атаковать/умирать, а не скользить статично.
Приоритет от S.: сначала статика+текстуры (сделано), потом анимация.

## Где лежит анимация

- **Скелет + скиннинг** — внутри самой модели `3dmob/<name>.gr2` (кости + веса вершин).
- **Треки движений** — отдельные файлы `3dmob_bone/<action>.gr2`:
  - `1_attack`, `2_damage`, `2_dead` — общие;
  - `7_/8_/9_ attack/damage/dead/move` — по «классу скелета» (7/8/9 = группы мобов).
  Каждый файл = granny_animation с TrackGroup: по кости кривые position/orientation/scaleshear во времени.

Маппинг action → RO-моушен (как в 2D): Idle(0), Walk(1), Attack(2), Hurt(3), Dead(4). Номер `N_` в имени
файла = «скелетная группа» модели (её надо прочитать из granny_skeleton/extended-data модели), а суффикс
(move/attack/damage/dead) = моушен.

## Что парсить (granny v6 32-бит — офсеты ВЫВЕРЕНЫ на aguardian90_8.gr2, 2026-07-15)

1. **granny_skeleton** — file_info: SkeletonCount@**32**, Skeletons@**36** (granny_skeleton**). ✅ ПРОВЕРЕНО:
   skel[0].Name='Dummy03', BoneCount=**43** (biped: Bip01/Bip01 Pelvis/Spine/R Thigh/...).
   - granny_skeleton: Name@**0**, BoneCount@**4**, Bones@**8**.
   - granny_bone: **stride = 156**; Name@**0**, ParentIndex@**4** (int, -1=корень), LocalTransform@**8**
     (granny_transform 68 б), InverseWorld4x4@**76** (16×f32 = готовая обратная bind-матрица для скиннинга).
2. **Веса вершин** — в vertex layout (parseVertexLayout уже читает Position/Normal/UV и правильно проматывает
   мимо костей). ✅ ПРОВЕРЕНО у скин-мешей: `Position`(Real32×3)@0, `BoneWeights`(NormalUInt8×4, type14)@**12**,
   `BoneIndices`(UInt8×4, type12)@**16**, `Normal`(Real32×3)@20, `TextureCoordinates0`(Real32×2)@32, stride 40.
   Жёсткие меши (навершия/камни) — только Pos/Normal/UV, без костей → привязаны к ОДНОЙ кости через
   mesh BoneBinding (найти индекс кости меша). Добавить в Gr2Mesh: `boneIdx[4]`,`boneW[4]` пер-вершина.
3. **granny_transform** (68 б): Flags(u32) + Position[3] + Orientation[4] (кватернион) + ScaleShear[3×3].
   Собрать в 4×4 (T·R·S).
4. **granny_animation** — file_info: AnimationCount@**80**, Animations@**84** (granny_animation**). ✅ ПРОВЕРЕНО
   на 3dmob_bone/8_move.gr2: 1 анимация, Name@0='...0127-HG2_WALK01.max' (ходьба), Duration@**4**=1.867с,
   TimeStep@**8**=0.0333 (~30 fps). Oversampling@12, TrackGroupCount@16, TrackGroups@20 (стандарт granny —
   ЕЩЁ выверить: @16/@20 у меня дали garbage, вероятно TrackGroups это granny_track_group* напрямую, не **).
   Сам файл 8_move.gr2 несёт скелет (43 кости, тот же biped) + 2 меша + анимацию — т.е. одна модель = поза,
   а треки берём из N_move/attack/damage/dead.gr2. 1_attack.gr2 = только анимация без меша (gr2Load→nullopt,
   т.к. бэйлит на meshCount==0 — при реализации грузить анимацию ОТДЕЛЬНЫМ путём, не через gr2Load).
5. **granny_track_group** (anim+20 = granny_track_group* НАПРЯМУЮ, не **): Name@**0**, VectorTrackCount@4,
   VectorTracks@8, **TransformTrackCount@12** (=43 у 8_move, по кости), **TransformTracks@16**
   (granny_transform_track[]). ✅ ПРОВЕРЕНО (трек[0].Name='Bip01 R Toe0Nub').
6. **granny_transform_track**: **стрид = 64** (✅). Name@**0**, Flags@**4**. Кривые ИНЛАЙН (не variant-указатели),
   формат каждой = **{KnotCount(int), Knots*(f32[]), ControlCount(int), Controls*(f32[])}** (DaK32fC32f). ✅ ПОЛНОСТЬЮ
   ДЕКОДИРОВАНО на 8_move track[2] 'Bip01 R Foot':
   - **Position** (константа): KnotCount@**+8**=1, Knots*@**+12** (1 время), ControlCount@**+16**=3, Controls*@**+20**
     (xyz = 6.2256,0,0 = позиция R Foot). ✓
   - поле @**+24** = 2 (Dimension/Degree — уточнить).
   - **Orientation**: KnotCount@**+28**=64, Knots*@**+32** (времена 0,0.058,0.087,0.117,...), ControlCount@**+36**=256
     (=64×4), Controls*@**+40** (64 кватерниона: 0.9954,0.0795,-0.0141,-0.0516,...). ✓✓ ЭТО данные ходьбы.
   - **ScaleShear** @**+44**: KnotCount@+44=0 (константа/идентичность), указатели @+52/+60.
   Один трек = разное число ключей на кривую; поле @+24 и точный размер каждой кривой ещё чуть уточнить, но
   формат данных ({kc,knots,cc,controls}) выверен — семплер писать МОЖНО.
7. **Кривая (Object)** — формат по имени variant.Type (granny_data_type_definition): DaConstant32f (одно
   значение — кость статична в этом клипе), DaK32fC32f (Knots[времена f32] + Controls[значения f32]),
   D4nK16uC15u (сжатые кватернионы) и т.д. Object содержит флоаты (у 8_move видны значения кватернионов).
   ← ЕДИНСТВЕННОЕ, что осталось реализовать: диспетч формата по Type + семплер (linear pos/scale, slerp quat).

## Семплер + рендер (реализация)
- Для позы t: по каждому треку взять кость (по Name→индекс скелета), семплировать 3 кривые → local
  transform кости (перебивает bind local). Кости без трека → bind local. Обход иерархии → world[b].
  skin[b] = world[b] · invBind[b] (учесть row/column-vector: local у меня column, invBind granny row).
- CPU-скин: v' = Σ weight_i · skin[boneIdx_i] · v. Кэш по (модель, action, кадр) как готовый VB.
- Жёсткие меши: v' = world[rigidBone] · v (одна кость).
- action→файл: idle=поза модели; walk=N_move; attack=N_attack/1_attack; hurt=N_damage/2_damage; dead=N_dead.
  N = группа скелета модели (7/8/9). Тайминги: dur/step из анимации; loop для walk/idle, once для attack/dead.

## Прогресс (2026-07-15) — ВСЁ выверено офлайн
- ✅ Скелет + скиннинг (cc529536): Gr2Model.bones (43 кости biped, local+invBind), boneIndices/boneWeights.
- ✅ Анимация (d62c3ed4): gr2LoadAnimation читает 3dmob_bone/*.gr2 → Gr2Animation{duration,timeStep,треки
  position+orientation}. Кривые = {KnotCount,Knots*,ControlCount,Controls*} на офсетах (8/12/16/20),
  (28/32/36/40),(48/52/56/60), классификация по dim (3=поз,4=кватернион). 8_move: 43 трека, dur 1.867,
  все |q|=1.0, времена монотонны.
- ✅ Семплер gr2SamplePose(bones,anim,t)→skin[] (d62c3ed4): lerp поз / slerp кватернион, world=parent·local,
  skin=world·invBind. 0 NaN, цикл замыкается (skin(0)=skin(dur)), поза движется (таз качается).
- ✅ mesh BoneBindings + invBind^T (701bd346): granny_mesh.BoneBindingCount@28/@32, стрид 36, BoneName@0
  (слот→кость по имени). InverseWorld4x4 хранится column-major → ТРАНСПОНИРОВАТЬ (иначе skin взрывается
  ×200; после T — identity err 0.0000 на bind-позе). Полный CPU-скин aguardian+8_move: bbox держится
  (orig 12.4×7.4×25.0 → поза 10.5×8.9×24.5), без взрыва.
- ✅ Render-обвязка (ветка feat/gr2-anim): Gr2Models::drawAnimated(action,time) — clipFor резолвит
  3dmob_bone/<группа>_<motion>.gr2 (группа=трейлинг _N модели; фолбэк 1_attack/2_damage/2_dead), семплит
  позу, CPU-скин в динамический VB на кадр, рисует. GameScene выбирает action по состоянию актёра
  (move.active→walk, attackUntil→attack, hurtUntil→hurt, dyingUntil→dead). Нужна bgfx-сборка для проверки.
- ⏳ Осталось: собрать ветку у S., подтвердить визуально; тюнинг тайминга/скорости, idle-клип (сейчас idle=bind).

## Скиннинг (рендер)

- CPU-скин на старте (проще, без нового шейдера): для позы t вычислить мировые матрицы костей
  (обход иерархии parent→child), `skinMat[b] = world[b] · invBind[b]`, затем на CPU трансформировать
  вершины `sum_i weight_i · skinMat[idx_i] · v`. Дорого пер-фрейм → кэшировать по (модель, action, кадр)
  как отдельные VB (мобов немного, кадров конечно).
- ИЛИ GPU-скин: залить skinMat[] юниформом/буфером, добавить в vs_model скиннинг по индексам/весам.
  Правильнее, но нужен отдельный шейдер (риск на ветку feat/gr2-anim, не в client/foundation).

## План по шагам

1. Расширить Gr2.hpp/Gr2.cpp: `Gr2Bone{name,parent,invBind[16],localXform[16]}`, `Gr2Model.bones`,
   а в Gr2Mesh — `boneIdx[4]`, `boneW[4]` пер-вершина (аддитивно, статичный путь не трогаем).
2. Парсер granny_animation для `3dmob_bone/*.gr2` (отдельный load): кости→треки→кривые.
3. Семплер позы: action + время → массив локальных трансформов костей → мировые → skinMat.
4. Рендер: CPU-скин с кэшем кадров (безопасно), позже GPU-скин на ветке.
5. Привязка: actor action (из 0x8a/движения) → выбор файла/моушена, тайминги как у 2D.

## Блокер / что нужно

Реализация требует РАСПАКОВАННОЙ анимированной модели для проверки офсетов (guildflag статичный, скелета
нет). Нужен один `gr2prep`-распакованный `aguardian90_8.gr2` (+ по возможности `3dmob_bone/8_move.gr2`) —
на нём выверю granny_skeleton/track офсеты и семплер, дальше распространю на все.
