# Sil-Q 日本語版の用語表（版 1.0、2026-08-20）

Sil-Q の日本語版で使う固有名詞と用語の一覧です。本文の翻訳（`silq/lang/ja/`）はこの表に
従っています。名前はこの表の 1 か所から全体に反映されるので、後から 1 語だけ直すこともできます。

---

## 0. 確度の印

| 印 | 意味 |
|---|---|
| ◎ | 評論社版の既訳（『シルマリルの物語』『指輪物語』『ホビットの冒険』）にある語です |
| ○ | 既訳があると思われますが、確認していない語です。音写の規則からは自然な形です |
| △ | 既訳がなく、ここで決めた語です |

◎ でも誤りがあるかもしれません。`出どころ` の欄が `当方` の行は、Sil-Q が独自に作った語で、
既訳はありません。

---

## 1. 画面の語（桁数が決まっているもの）

Sil-Q の画面は `defines.h:679-760` に表示位置がバイト単位で書かれています。訳が桁数を
超えると隣の項目を消してしまうので、ここは特に注意が必要です。

### 1.1 左の状態列（13 桁）

| 英語 | 桁（バイト） | 日本語（案） | 訳のバイト | 確度 | 註 |
|---|---|---|---|---|---|
| `Health` / `Hth` | 12 | 生命力 | 6 | △ | 短い側（`Hth`）は日本語では要らない。どちらも「生命力」で足りる |
| `Voice` / `Vce` | 12 | 声 | 2 | 決 | Sil 独自の資源（歌を歌う力）。§3 も見よ |
| `Exp ` | 4 ＋数 8 | 経験 | 4 | ◎ | 変愚と同じ |
| `Str` / `Dex` / `Con` / `Gra` | 4 | 腕力 / 敏捷 / 耐久 / 恩寵 | 各 4 | 決 | `Gra` だけ Sil 独自。§3 |
| `Mel` / `Arc` / `Evn` / `Stl` / `Per` / `Wil` / `Cmt` / `Smt` / `Sng` | 3 | 打 / 射 / 避 / 隠 / 知 / 意 / 鍛 / 歌 | 各 2 | △ | 3 桁に 2 バイト。漢字 1 字ずつに切る。§3 |
| `Resistances` | 12 | 耐性 | 4 | ◎ | |
| `Bleeding` | 11 | 出血 | 4 | ◎ | |
| `Poisoned` | 11 | 毒 | 2 | ◎ | |
| `Mortal wound` | 12 | 致命傷 | 6 | ◎ | |
| `Surface` | 12（右詰め） | 地上 | 4 | ◎ | |
| `%d ft` | 12（右詰め） | `550 ft`（英語のまま） | — | 決 | 決めたこと（問 1）。単位は英字のまま出す |
| `min %d ft` | 12（右詰め） | `min 550 ft`（英語のまま） | — | 決 | 同上 |

### 1.2 最下段の帯（桁が固定）

`xtra1.c:598-933` と `defines.h:733-754` にあります。桁は開始位置で、次の項目までが使える幅です。

| 英語 | 開始桁 | 使える幅 | 日本語（案） | 訳のバイト | 確度 |
|---|---|---|---|---|---|
| `Starving` | 13 | 9 | 飢餓 | 4 | ◎ |
| `Weak` | 13 | 9 | 衰弱 | 4 | ◎ |
| `Hungry` | 13 | 9 | 空腹 | 4 | ◎ |
| `Full` | 13 | 9 | 満腹 | 4 | ◎ |
| `Blind` | 22 | 6 | 盲目 | 4 | ◎ |
| `Confused` | 28 | 9 | 混乱 | 4 | ◎ |
| `Stun`（軽） | 37 | 12 | よろけ | 6 | 決 |
| `Heavy stun`（中） | 37 | 12 | 朦朧 | 4 | 決 |
| `Knocked out`（重） | 37 | 12 | 昏倒 | 4 | 決 |
| `Afraid` | 49 | 7 | 恐怖 | 4 | ◎ |
| `Stealth`（状態） | 56 | 11 | 隠密行動 | 8 | ○ |
| `Entranced!` | 56 | 11 | 幻惑！ | 6 | △ |
| `Smithing` | 56 | 11 | 鍛冶中 | 6 | ○ |
| `Slow` / `Fast` | 67 | 5 | 鈍足 / 加速 | 4 | ◎ |
| `Web` | 72 | 8 | 蜘蛛の巣 | 8 | ◎ |
| `Pit` | 72 | 8 | 落し穴 | 6 | ◎ |
| `Sunlight` | 72 | 8 | 陽光 | 4 | ◎ |

---

## 2. 能力値・技能

Sil-Q にはレベルも金銭もなく、能力値 4 つと技能 8 つで全てが決まります。変愚蛮怒の訳語を
そのまま使うと、あちらにない概念が抜けてしまうので、別に決めています。

| 英語 | 日本語（案） | 出どころ | 確度 | 註 |
|---|---|---|---|---|
| Strength | 腕力 | 変愚に倣う | ◎ | |
| Dexterity | 敏捷 | 変愚に倣う | ◎ | |
| Constitution | 耐久 | 変愚に倣う | ◎ | |
| Grace | 恩寵 | 当方 | 決 | Sil 独自の 4 つ目。歌・鍛冶・意志に効く。決めたこと（問 3） |
| Melee | 打撃 | 変愚に倣う | ◎ | |
| Archery | 射撃 | 変愚に倣う | ◎ | |
| Evasion | 回避 | 当方 | ○ | 命中を避ける値。「体さばき」も案 |
| Stealth | 隠密 | 変愚に倣う | ◎ | |
| Perception | 知覚 | 変愚に倣う | ◎ | |
| Will | 意志 | 変愚に倣う | ◎ | |
| Smithing | 鍛冶 | 当方 | ○ | |
| Song | 歌唱 | 当方 | ○ | |
| Protection | 防護 | 当方 | ○ | 受けた打撃を減らす値（防御力ではない） |

---

## 3. Sil 独自の用語（既訳がなく、ここで決めたもの）

| 英語 | 日本語（案） | 確度 | 註 |
|---|---|---|---|
| Voice | 声 | 決 | 歌を歌う資源（変愚の MP に当たる）。決めたこと（問 4） |
| Song of ... | 〜の歌 | ○ | 下の一覧 |
| Song of Elbereth | エルベレスの歌 | ◎ | Elbereth は既訳あり |
| Song of Challenge | 挑みの歌 | △ | |
| Song of Delvings | 坑道の歌 | △ | delving＝掘り進んだ坑。ドワーフの歌 |
| Song of Freedom | 解放の歌 | ○ | |
| Song of Silence | 静寂の歌 | ○ | |
| Song of Staunching | 止血の歌 | ○ | |
| Song of Thresholds | 敷居の歌 | △ | 扉を封じる歌 |
| Song of the Trees | 二本の木の歌 | ○ | Two Trees を指すなら「二本の木」 |
| Song of Slaying | 殺戮の歌 | ○ | |
| Song of Staying | 不動の歌 | △ | |
| Song of Lorien | ローリエンの歌 | ◎ | ヴァラのローリエン（眠りを司る） |
| Song of Mastery | 制圧の歌 | △ | フィンロドとサウロンの歌合戦の「力の歌」 |
| Woven Themes | 織り合わせ | △ | 歌を 2 つ重ねる技 |
| forge（名詞） | 鍛冶場 | ○ | |
| forge（動詞） | 鍛える | ○ | |
| thrall | 虜囚 | △ | `Alert elven thrall` → 「油断のないエルフの虜囚」。「奴隷」「囚われ人」も案 |
| thrallmaster | 虜囚頭 | △ | `Orc thrallmaster` |
| rauko（系） | 〜の魔霊 | 決 | 決めたこと（問 5「意訳」）。`rauko`＝力ある悪霊（`Valarauko`＝バルログ の後半）。`D:` が 6 体とも「spirit」と書いているので「魔霊」を当てた |
| Sulrauko | 風の魔霊 | 決 | *sul*＝風。`A wind spirit, bent to Morgoth's will.` |
| Ringrauko | 氷の魔霊 | 決 | *ring*＝冷。`A chill hangs in the air ... A cold gleaming blade` |
| Kemenrauko | 岩の魔霊 | 決 | *kemen*＝地。`the very stone from which Angband is hewn` なので「地」より「岩」 |
| Gwathrauko | 闇の魔霊 | 決 | *gwath*＝影だが `D:` は `A spirit of Darkness itself`。`Shadow`（影）という別の敵が居るので衝突を避けて「闇」 |
| Hithrauko | 霧の魔霊 | 決 | *hith*＝霧。`many smokes and strange airs` |
| Ururauko | 焔の魔霊 | 決 | *ur*＝火。`A form of pure flame` |
| Silmaril | シルマリル | ◎ | |
| Balrog | バルログ | ◎ | |
| Easterling | 東夷 | ◎ | 『シルマリルの物語』の既訳 |

---

## 4. 種族・家・地名

| 英語 | 日本語 | 出どころ | 確度 |
|---|---|---|---|
| Noldor | ノルドール | シルマリル | ◎ |
| Sindar | シンダール | シルマリル | ◎ |
| Naugrim | ナウグリム | シルマリル | ◎ |
| Edain | エダイン | シルマリル | ◎ |
| Houseless | 家を持たぬ者 | 当方 | △ |
| House of Feanor | フェアノールの家 | シルマリル | ◎ |
| House of Fingolfin | フィンゴルフィンの家 | シルマリル | ◎ |
| House of Finarfin | フィナルフィンの家 | シルマリル | ◎ |
| Of Doriath | ドリアスの者 | シルマリル | ◎ |
| House of Nogrod | ノグロドの家 | シルマリル | ◎ |
| House of Belegost | ベレゴストの家 | シルマリル | ◎ |
| House of Beor | ベオルの家 | シルマリル | ◎ |
| House of Haleth | ハレスの家 | シルマリル | ◎ |
| House of Hador | ハドルの家 | シルマリル | ◎ |
| Falathrim / the Falas | ファラスリム / ファラス | シルマリル | ◎ |
| Beleriand | ベレリアンド | シルマリル | ◎ |
| Angband | アングバンド | シルマリル | ◎ |
| Utumno | ウトゥムノ | シルマリル | ◎ |
| Gondolin | ゴンドリン | シルマリル | ◎ |
| Cuivienen | クイヴィエーネン | シルマリル | ◎ |
| Brethil | ブレシル | シルマリル | ◎ |
| Valinor | ヴァリノール | シルマリル | ◎ |
| Helcaraxe | ヘルカラクセ | シルマリル | ◎ |
| Amon Rudh | アモン・ルーズ | シルマリル | ◎ |
| Dor-Lomin | ドル＝ローミン | シルマリル | ◎ |
| Nevrast | ネヴラスト | シルマリル | ◎ |
| Taur-nu-Fuin | タウア＝ヌ＝フイン | シルマリル | ◎ |
| Sirion | シリオン | シルマリル | ◎ |
| Dolmed | ドルメド | シルマリル | ◎ |
| Eruman | エルマン | シルマリル | ◎ |
| Gorgoroth | ゴルゴロス | シルマリル | ◎ |
| Dwarrowdelf | ドワローデルフ | 指輪 | ◎ |
| Avernien | アルヴェルニエン（綴りは `Arvernien` のはず。Sil-Q 側の誤記か。要確認） | シルマリル | △ |

---

## 5. ユニークモンスター（トールキンの固有名詞）

`silq/lib/edit/monster.txt` の固有名を持つユニーク全てです。称号は訳し、名前は音写しています。

| 英語 | 日本語（案） | 確度 | 註 |
|---|---|---|---|
| Morgoth, Lord of Darkness | 暗黒の王モルゴス | ◎ | |
| Morgoth, ... (without crown) | 暗黒の王モルゴス（王冠なし） | ◎ | |
| Melkor, Rightful Lord of Arda | アルダの正統なる王メルコール | ◎ | |
| Gothmog, High Captain of Balrogs | バルログの長ゴスモグ | ◎ | |
| Ungoliant, the Gloomweaver | 闇を織る者ウンゴリアント | ◎ | |
| Glaurung, the Deceiver | 欺く者グラウルング | ◎ | |
| Ancalagon the Black | 黒のアンカラゴン | ◎ | |
| Carcharoth, the Jaws of Thirst | 渇きの顎カルハロス | ◎ | |
| Draugluin, Sire of Werewolves | 人狼の祖ドラウグルイン | ◎ | |
| Thuringwethil, the Vampire Messenger | 吸血鬼の使者スリングウェシル | ○ | |
| Shelob, Spider of Darkness | 闇の蜘蛛シェロブ | ◎ | 指輪 |
| Scatha the Worm | 蛇スカサ | ○ | 指輪 追補編 |
| Smaug the Golden | 黄金のスマウグ | ◎ | ホビット |
| Maeglin, Betrayer of Gondolin | ゴンドリンを売りしマエグリン | ◎ | |
| Feanor, High King of the Noldor | ノルドールの上級王フェアノール | ◎ | |
| Tevildo, Prince of Cats | 猫の君テヴィルド | △ | 『失われた物語』。評論社版に無い |
| Oikeroi, Guard of Tevildo | テヴィルドの衛士オイケロイ | △ | 同上 |
| Umuiyan, the Doorkeeper | 門番ウムイヤン | △ | 同上 |
| Gorgol, the Butcher | 屠殺者ゴルゴル | ◎ | |
| Boldog, the Merciless | 情け知らずのボルドグ | ○ | |
| Orcobal, Champion of the Orcs | オーク一の勇士オルコバル | △ | HoME 由来 |
| Othrod, the Orc Lord | オーク王オスロド | △ | Sil 独自 |
| Uldor, the Accursed | 呪われしウルドル | ◎ | |
| Ulfang the Black | 黒のウルファング | ◎ | |
| Beren, Son of Barahir | バラヒアの息子ベレン | ◎ | |
| Luthien Tinuviel | ルーシエン・ティヌーヴィエル | ◎ | |
| Huan, Hound of Valinor | ヴァリノールの猟犬フアン | ◎ | |
| Thingol, the Hidden King | 隠れたる王シンゴル | ◎ | |
| Thorondor, King of Eagles | 鷲の王ソロンドール | ◎ | |
| Eagle of Manwe | マンウェの鷲 | ◎ | |
| Gorthaur, Servant of Morgoth | モルゴスの下僕ゴルサウル | ◎ | サウロンの別名 |
| Gilim, the Giant of Eruman | エルマンの巨人ギリム | ○ | |
| Nan, the Giant | 巨人ナン | ○ | |
| Aldor, the Risen King | 甦りし王アルドル | △ | Sil 独自 |
| Balcmeg, the Relentless | 執拗なるバルクメグ | △ | HoME 由来 |
| Dagorhir, the Elfbane | エルフの禍ダゴルヒア | △ | Sil 独自 |
| Gostir, the Dread Glance | 恐ろしき眼差しゴスティア | △ | Sil 独自 |
| Lug, the Grotesque | 醜怪なるルグ | △ | Sil 独自 |
| Delthaur, Balrog of Terror | 恐怖のバルログ、デルサウル | △ | Sil 独自 |
| Belegwath, Balrog of Shadow | 影のバルログ、ベレグワス | △ | Sil 独自 |
| Vallach, Balrog of Sudden Flame | 突然の焔のバルログ、ヴァラハ | △ | *Dagor Bragollach*（にわかに焔流るる合戦）に掛かる |
| Turkano, Balrog of the Hosts | 軍勢のバルログ、トゥルカノ | △ | Sil 独自 |
| Duruin, Least of the Balrogs | 最も小さきバルログ、ドゥルイン | △ | Sil 独自 |
| Lungorthin, Lord of Balrogs | バルログの主ルングォルシン | ○ | HoME 由来 |
| Aule, the Smith | 鍛冶のアウレ | ◎ | 以下ヴァラ |
| Manwe, Lord of the Breath of Arda | アルダの息吹の主マンウェ | ◎ | |
| Varda, Lady of the Stars | 星々の后ヴァルダ | ◎ | |
| Ulmo, Lord of Waters | 水の主ウルモ | ◎ | |
| Yavanna, the Giver of Fruits | 果実を与える者ヤヴァンナ | ◎ | |
| Orome, Lord of Forests | 森の主オロメ | ◎ | |
| Mandos, Doomsman of the Valar | ヴァラールの審判者マンドス | ◎ | |
| Lorien, Master of Dreams | 夢の主ローリエン | ◎ | |
| Nienna, Lady of Mourning | 嘆きの后ニエンナ | ◎ | |
| Tulkas, the Valiant | 勇猛なるトゥルカス | ◎ | |
| Nessa, the Dancer | 踊り手ネッサ | ◎ | |
| Vana, the Ever Young | 永遠に若きヴァーナ | ◎ | |
| Vaire, the Weaver | 織り手ヴァイレ | ◎ | |
| Este, the Healer | 癒し手エステ | ◎ | |

---

## 6. アーティファクトの銘

銘は常に前に置きます。`'Elessar'` → `『エレッサール』`、`of Barahir` → `バラヒアの`。
後ろには置きません（変愚蛮怒と同じ規則です）。

### 6.1 固有の銘（`'...'`）

| 英語 | 日本語 | 確度 |
|---|---|---|
| 'Narsil' | 『ナルシル』 | ◎ |
| 'Glamdring' | 『グラムドリング』 | ◎ |
| 'Orcrist' | 『オルクリスト』 | ◎ |
| 'Angrist' | 『アングリスト』 | ◎ |
| 'Anguirel' | 『アングイレル』 | ◎ |
| 'Aranruth' | 『アランルース』 | ◎ |
| 'Aeglos' | 『アイグロス』 | ◎ |
| 'Ringil' | 『リンギル』 | ◎ |
| 'Grond' | 『グロンド』 | ◎ |
| 'Elessar' | 『エレッサール』 | ◎ |
| 'Nimphelos' | 『ニンフェロス』 | ◎ |
| 'Dramborleg' | 『ドランボルレグ』 | ◎ |
| 'Belthronding' | 『ベルスロンディング』 | ◎ |
| 'Dailir' | 『ダイリル』 | ○ |
| 'Calris' | 『カルリス』 | △ |
| 'Celeg Aithorn' | 『ケレグ・アイソルン』 | △ |
| 'Glend' | 『グレンド』 | △ |
| 'Silverhand' | 『シルヴァーハンド』 | 決 |
| 'Starlight' | 『スターライト』 | 決 |
| 'Thorn' | 『ソーン』 | 決 |
| 'Catskin' | 『キャットスキン』 | 決 |
| 'Death's Sting' | 『デスズ・スティング』 | 決 |
| 'Ungoliant's Lament' | 『ウンゴリアントの嘆き』 | ○ |
| 'Ultimate' | 『アルティメット』 | 決 |
| 'Luinmegil' | 『ルインメギル』（*luin*＝青・*megil*＝剣） | △ |
| 'Tawarcun' | 『タワルクン』 | △ |
| 'Thandrach' | 『サンドラハ』 | △ |
| 'Agarlhang' | 『アガルハング』 | △ |
| 'Celebrist' | 『ケレブリスト』 | △ |
| 'Dagmor' | 『ダグモル』（ベレンの剣） | ○ |
| 'Azdulag' / 'Binahkram' / 'Burkfelek' / 'Dugrakh' / 'Mazarbulbark' / 'Nakh' | 『アズドゥラグ』/『ビナフクラム』/『ブルクフェレク』/『ドゥグラフ』/『マザルブルバルク』/『ナフ』（ドワーフ語＝クズドゥル。音写の当て方を特に見てほしい） | △ |

### 6.2 `of ...` の銘（人名・地名は §4 §5 と揃える）

原則は `of X` → `X の` です。人名は §5 の音写に合わせます。下には既訳のあるものだけを
挙げています（残りは §5 の表を参照してください）。

| 英語 | 日本語 | 確度 |
|---|---|---|
| of Barahir | バラヒアの | ◎ |
| of Beren | ベレンの | ◎ |
| of Luthien | ルーシエンの | ◎ |
| of Melian | メリアンの | ◎ |
| of Thingol | シンゴルの | ◎ |
| of Feanor | フェアノールの | ◎ |
| of Fingolfin | フィンゴルフィンの | ◎ |
| of Fingon | フィンゴンの | ◎ |
| of Finrod | フィンロドの | ◎ |
| of Finwe | フィンウェの | ◎ |
| of Maedhros | マエズロスの | ◎ |
| of Maglor | マグロールの | ◎ |
| of Celegorm | ケレゴルムの | ◎ |
| of Curufin | クルフィンの | ◎ |
| of Amrod | アムロドの | ◎ |
| of Aegnor | アイグノールの | ◎ |
| of Angrod | アングロドの | ◎ |
| of Aredhel | アレゼルの | ◎ |
| of Orodreth | オロドレスの | ◎ |
| of Galadriel | ガラドリエルの | ◎ |
| of Glorfindel | グロールフィンデルの | ◎ |
| of Ecthelion | エクセリオンの | ◎ |
| of Idril Celebrindal | イドリル・ケレブリンダルの | ◎ |
| of Celebrimbor | ケレブリンボールの | ◎ |
| of Daeron | ダイロンの | ◎ |
| of Rumil | ルーミルの | ◎ |
| of Telchar | テルハルの | ◎ |
| of Durin | ドゥリンの | ◎ |
| of Beor | ベオルの | ◎ |
| of Hador | ハドルの | ◎ |
| of Gundor | グンドルの | ◎ |
| of Halmir | ハルミルの | ◎ |
| of Haldad | ハルダドの | ◎ |
| of Bregolas | ブレゴラスの | ◎ |
| of Gorlim | ゴルリムの | ◎ |
| of Andreth | アンドレスの | ◎ |
| of Maeglin | マエグリンの | ◎ |
| of Draugluin | ドラウグルインの | ◎ |
| of Thuringwethil | スリングウェシルの | ○ |
| of Boldog | ボルドグの | ○ |
| of Morgoth | モルゴスの | ◎ |
| of Melkor | メルコールの | ◎ |
| of Mairon | マイロンの（サウロンの真名） | ○ |
| of Aule's Wrath | アウレの怒りの | ◎ |
| of Mothers' Woe | 母たちの悲しみの | △ |
| of the Bloody Hand | 血の手の | △ |
| of the Oathbreaker | 誓いを破りし者の | ○ |
| of the Mole | もぐらの（マエグリンの紋章） | △ |
| of the Swan | 白鳥の | ◎ |
| of the Swallow | 燕の | ◎ |
| of the Dwarves | ドワーフの | ◎ |
| of the Dwarrowdelf | ドワローデルフの | ◎ |
| of the Sirion | シリオンの | ◎ |
| of the Helcaraxe | ヘルカラクセの | ◎ |
| of Nargil / of Ogbar / of Gaurin / of Saithnar | ナルギルの / オグバルの / ガウリンの / サイスナルの | △ |

---

## 7. 表記の規則（訳語ではなく規則）

訳すときに毎回迷わないよう、規則をここにまとめます。

| 決定 | 中身 |
|---|---|
| 冠詞 `&` | 落とす。`& Dagger~` → `ダガー` |
| 複数 `~` | 落として助数詞にする。`ダガー3本` / `矢12本` / `鎖帷子1着` |
| 助数詞の持ち方 | `object.ja.txt` の註記行 `#counter:本` |
| 銘の位置 | 常に前。`守りの鎖帷子` / `『エレッサール』の指輪` |
| 未識別の呼び名 | `flavor` の語を前に。`紫水晶の指輪` |
| 敵の冠詞 | `the` / `a` / `it` は落とす。見えない敵は `何か`、複数形は付けない |
| `%^s` | 日本語では何もしない |
| 敵の称号 | 称号を前・名を後（`暗黒の王モルゴス`）。英語の `Morgoth, Lord of Darkness` の順を保たない |

---

## 8. 訳さないもの

| もの | 理由 |
|---|---|
| `names.txt`（601 語） | ランダム銘の音節。音写もしません |
| `vault.txt` の `D:` | `D:` は地図の行であって説明ではない |
| プレイヤ名 | M1 ではローマ字入力のままです |
| `names.txt` の音節 | 上に同じ |

---

## 9. 決定した 6 点（2026-08-20）

| # | 問い | 決定 | 効く先 |
|---|---|---|---|
| 1 | 深さの単位 | `ft` 等は英語表記のまま | §1.1 の `%d ft` / `min %d ft`。`Surface` は「地上」と訳す（単位ではないので） |
| 2 | 朦朧の段 | よろけ → 朦朧 → 昏倒 | §1.2 |
| 3 | `Grace` | 恩寵 | §1.1 / §2 |
| 4 | `Voice` | 声 | §1.1 / §3 |
| 5 | `rauko` 系 6 体 | 意訳（`〜の魔霊`） | §3。当て方は §3 の表を見よ |
| 6 | 意味の見える銘 | 音写する | §6.1。`'Silverhand'` → `『シルヴァーハンド』` |

`決` の印は、確度ではなく「決定した」ことを表します。`決` の付いた行はそのままにしてください。
それ以外の行はいつでも直せます。名前は `silq/lang/ja/` の 1 か所から全体に反映されるので、
後から 1 語だけ差し替えても全体に効きます。

---

## 10. 同じ英語を 2 通りに訳したもの

場所によって訳し分けたものはここに全部挙げています。挙げておかないと、後で表記の揺れに
見えて片方に寄せられ、区別が消えてしまうためです。

| 英語 | 場所 | 訳 | なぜ分けたか |
|---|---|---|---|
| `Protection` | 技能・防護のダイス | 防護 | 打撃を減らす値そのもの |
| `Protection` | アイテムの銘（`of Protection`） | 守りの | `守りの鎖帷子` のほうが読める（用語表 §7 の例） |
| `Slow` | 最下段の帯（状態） | 鈍足 | 状態異常 |
| `Slowness` | 薬の銘 | 遅鈍 | 同じ字にすると「薬を飲んだ」のか「状態異常になった」のか読み分けられない |
| `Majesty` | 杖の銘・能力 | 威光 | どちらも同じ働き（敵を恐れさせる）なので揃えた |
| `Warding` | 杖の銘 | 守り | 地形の `glyph of warding`（守りのルーン）と同じ語源 |
| `Terror` | 角笛の銘・薬草 | 恐慌 | 最下段の帯の `Afraid`（恐怖）と別の語にする |
| `Follow-Through` | 能力 | 追い討ち | `Rout`（敗走狙い）と衝突させない |
| `Rout` | 能力 | 敗走狙い | 「敗走」ではなく逃げる敵を狙い撃つ技（`D:` を読んだ） |
| `Channeling` | 能力 | 交感 | 「導管」ではなく杖と角笛を使いこなす技 |
| `Outwit` | 能力 | 出し抜き | 相手の会心を知覚で打ち消す技 |
| `Gwathrauko` | 敵 | 闇の魔霊 | *gwath*＝影だが、`Shadow`（影）という別の敵が居るので衝突を避けた |

---

## 11. この表の外にあるもの

- 一般名詞（`Cave troll` `Orc archer` `& Dagger~` `open floor` など）はこの表に載せていません。
  数が多く（約 930 件）、既訳の当たりも付いているので、本文の翻訳の中でまとめて訳しています。
  ただし、この表の語を含むもの（`Balrog` `Easterling` `thrall` など）はここの決定に従います。
- メッセージ（約 3 万字）と説明文（約 6 万字）は別に訳しています。
