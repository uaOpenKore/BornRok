# GRF: корейские названия → английские алиасы

> Задача от Сергио: миграция GRF с корейских (EUC-KR) имён на английские. Клиент теперь ищет
> **английский путь первым** (`Vfs::read` → `aliasKoreanPath`, файл `src/resource/GrfAlias.hpp`),
> и только если английского файла нет — берёт корейский. Так контент-мейкер постепенно
> переименовывает файлы в английские, ничего не ломая: положил `data/sprite/human/body/male/
> novice_male.spr` — клиент возьмёт его вместо корейского `인간족\몸통\남\초보자_남.spr`.

## Как это работает

`Vfs::read` переводит **корейские токены пути** в английские по таблице ниже и сначала пробует
английский путь (в GRF и в loose-папках), потом корейский. Перевод покомпонентный: путь режется
по `\` и `/`, каждый компонент — либо целиком из таблицы, либо по под-токенам через `_`
(`<джоб>_<пол>` → `novice_male`), расширение (`.spr`/`.act`) сохраняется. Чисто-английские пути
не трогаются.

## Что переведено

В GRF **111 корейских папок** и ~17 000 корейских имён файлов. В таблицу-алиасов засеяны:
**папки структуры спрайтов, части тела, пол, все джобы, города/карты** (137 записей). Полный
перевод 17k файлов — длинный хвост: контент-мейкер дополняет таблицу в `GrfAlias.hpp` по мере
переименования (или просто кладёт английские файлы по уже-известным английским папкам).

## Таблица алиасов (корейский → английский)

| Корейское имя (GRF) | Английский алиас |
|---|---|
| `인간족` | `human` |
| `몸통` | `body` |
| `머리통` | `head` |
| `머리` | `hair` |
| `남` | `male` |
| `여` | `female` |
| `몸` | `body_pal` |
| `악세사리` | `accessory` |
| `로브` | `robe` |
| `방패` | `shield` |
| `몬스터` | `monster` |
| `이팩트` | `effect` |
| `용병` | `mercenary` |
| `아이템` | `item` |
| `유저인터페이스` | `userinterface` |
| `페코페코_기사` | `pecopeco_knight` |
| `신페코크루세이더` | `newpeco_crusader` |
| `구페코크루세이더` | `oldpeco_crusader` |
| `천사날개` | `angel_wings` |
| `타락천사의날개` | `fallen_angel_wings` |
| `모험가배낭` | `adventurer_backpack` |
| `초보자` | `novice` |
| `검사` | `swordman` |
| `마법사` | `mage` |
| `궁수` | `archer` |
| `성직자` | `acolyte` |
| `상인` | `merchant` |
| `도둑` | `thief` |
| `기사` | `knight` |
| `프리스트` | `priest` |
| `위저드` | `wizard` |
| `제철공` | `blacksmith` |
| `헌터` | `hunter` |
| `어세신` | `assassin` |
| `크루세이더` | `crusader` |
| `몽크` | `monk` |
| `세이지` | `sage` |
| `로그` | `rogue` |
| `연금술사` | `alchemist` |
| `바드` | `bard` |
| `무희` | `dancer` |
| `슈퍼노비스` | `supernovice` |
| `건너` | `gunslinger` |
| `닌자` | `ninja` |
| `태권소년` | `taekwon` |
| `권성` | `star_gladiator` |
| `소울링커` | `soul_linker` |
| `로드나이트` | `lord_knight` |
| `하이프리` | `high_priest` |
| `하이위저드` | `high_wizard` |
| `화이트스미스` | `whitesmith` |
| `스나이퍼` | `sniper` |
| `어쌔신크로스` | `assassin_cross` |
| `팔라딘` | `paladin` |
| `챔피온` | `champion` |
| `프로페서` | `professor` |
| `스토커` | `stalker` |
| `크리에이터` | `creator` |
| `클라운` | `clown` |
| `집시` | `gypsy` |
| `룬나이트` | `rune_knight` |
| `워록` | `warlock` |
| `레인져` | `ranger` |
| `아크비숍` | `arch_bishop` |
| `미케닉` | `mechanic` |
| `길로틴크로스` | `guillotine_cross` |
| `로얄가드` | `royal_guard` |
| `슈라` | `sura` |
| `소서러` | `sorcerer` |
| `민스트럴` | `minstrel` |
| `제네릭` | `geneticist` |
| `쉐도우체이서` | `shadow_chaser` |
| `마도기어` | `mado_gear` |
| `운영자` | `gm` |
| `운영자2` | `gm2` |
| `산타` | `santa` |
| `결혼` | `wedding` |
| `한복` | `hanbok` |
| `턱시도` | `tuxedo` |
| `기타마을` | `etc_town` |
| `기타마을내부` | `etc_town_indoor` |
| `필드바닥` | `field_ground` |
| `내부소품` | `indoor_props` |
| `외부소품` | `outdoor_props` |
| `나무잡초꽃` | `tree_grass_flower` |
| `워터` | `water` |
| `던전` | `dungeon` |
| `전장` | `battlefield` |
| `길드전` | `woe` |
| `지하감옥` | `prison` |
| `지하묘지` | `catacombs` |
| `용암동굴` | `lava_cave` |
| `흑마법사방` | `warlock_room` |
| `워프대기실내부` | `warp_room` |
| `프론테라` | `prontera` |
| `프론테라내부` | `prontera_indoor` |
| `페이욘` | `payon` |
| `페이욘내부` | `payon_indoor` |
| `게페니아` | `geffenia` |
| `게펜내부` | `geffen_indoor` |
| `모로코` | `morocc` |
| `모로코내부` | `morocc_indoor` |
| `아인브로크` | `einbroch` |
| `라헬` | `rachel` |
| `리히타르젠` | `lighthalzen` |
| `휘겔` | `hugel` |
| `글래스트` | `glastheim` |
| `글래지하수로` | `glast_underwater` |
| `유노` | `juno` |
| `유노추가` | `juno_ext` |
| `알데바란` | `aldebaran` |
| `알베르타` | `alberta` |
| `알베르타내부` | `alberta_indoor` |
| `니플헤임` | `niflheim` |
| `타나토스` | `thanatos` |
| `토르화산` | `thor_volcano` |
| `어비스` | `abyss` |
| `유페로스` | `yuferos` |
| `아요타야` | `ayothaya` |
| `움발라` | `umbala` |
| `자와이` | `jawaii` |
| `등대섬` | `lighthouse_island` |
| `거북이섬` | `turtle_island` |
| `무명섬` | `nameless_island` |
| `해변마을` | `comodo` |
| `사막도시` | `morocc_desert` |
| `크리스마스마을` | `xmas` |
| `동굴마을` | `cave_town` |
| `집시마을` | `gypsy_town` |
| `히나마쯔리` | `hinamatsuri` |
| `중국` | `china` |
| `일본` | `japan` |
| `러시아` | `russia` |
| `대만` | `taiwan` |
| `브라질 몬스터` | `brazil_monster` |
| `인던01` | `indoor_dungeon01` |
| `인던02` | `indoor_dungeon02` |
## Примеры путей

| Корейский (как в GRF) | Английский (что положить) |
|---|---|
| `data\sprite\인간족\몸통\남\초보자_남.spr` | `data\sprite\human\body\male\novice_male.spr` |
| `data\sprite\인간족\머리통\남\1_남.spr` | `data\sprite\human\head\male\1_male.spr` |
| `data\sprite\악세사리\남\남_…` | `data\sprite\accessory\male\male_…` |
| `data\sprite\몬스터\poring.spr` | `data\sprite\monster\poring.spr` (имя уже англ.) |

## Замена текстур земли (loose PNG, без корейских имён)

Текстуры земли карт названы **по-корейски прямо в имени файла** (не только в папке). Напр.
`prontera.gnd` ссылается на `필드바닥\prt_도시01.bmp` (`도시` = «город»). Никакого
`prt_city_bot01` не существует — это часто путают.

Чтобы заменить текстуру земли своим рисунком, положи **PNG рядом с exe** по
**полностью английскому** пути — алиас сам сопоставит его с корейским оригиналом (токены имён
файла тоже переводятся, хвост из цифр сохраняется: `도시01` ↔ `city01`):

| Корейский оригинал (в GRF) | Английский PNG (что положить рядом с exe) |
|---|---|
| `data\texture\필드바닥\prt_도시01.bmp` | `data\texture\field_ground\prt_city01.png` |
| `data\texture\필드바닥\prt_초원09.bmp` | `data\texture\field_ground\prt_grass09.png` |
| `data\texture\필드바닥\prt_흙03.bmp` | `data\texture\field_ground\prt_dirt03.png` |

Токены имён земли: `도시`=city, `초원`=grass, `흙`=dirt, `모래`=sand, `눈`=snow, `잔디`=lawn,
`바닥`=floor, `벽`=wall, `길`=road, `물`=water_tile. **Только ASCII** — так файл гарантированно
откроется на любой Windows-локали (корейское имя loose-файла может не открыться на не-корейской
системе). Текстуры кэшируются на загрузку карты — перезайди на карту, чтобы увидеть подмену.

## Расширение таблицы

Добавлять записи в `src/resource/GrfAlias.hpp` (`koreanAliasMap()`): ключ — корейские байты
EUC-KR (`\xNN…`), значение — строчный английский алиас. После добавления — пересобрать.
Для непереведённых корейских токенов клиент молча берёт корейский файл (обратная совместимость).
