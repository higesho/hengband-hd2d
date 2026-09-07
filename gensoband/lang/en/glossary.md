# 幻想蛮怒 英語版の用語表（版 0.9、2026-08-24）

英語版の固有名詞はこの表に従います。`lang/en/edit/*.en.txt` と `messages.en.txt` の訳語も
ここに合わせています。

## 0. 方針

| # | 方針 |
|---|---|
| 1 | 名前の並びは公式の対外表記に従う。 洋順（名・姓: Reimu Hakurei）が基本。「の」を含む名（Fujiwara no Mokou・Mononobe no Futo・Soga no Tojiko・Hata no Kokoro・Toyosatomimi no Miko・Hieda no Akyuu・Watatsuki no Yorihime/Toyohime）と中華姓（Hong Meiling・Son Biten）は公式どおり原順のまま |
| 2 | 二つ名『名前』は「Name, the Title」の形（変愚在来のユニーク `Oberon, King of Amber` と同じ）。二つ名は意訳し、公式プロフィールの英語があるものは寄せる |
| 3 | 変愚在来のもの（ID 1088 以前のユニーク・アイテム・地形・呪文）は `#else`／`E:` の英語が正。直さない |
| 4 | 綴りはヘボン式へ寄せる（Syameimaru→Shameimaru・Kotiya→Kochiya・Hiziri→Hijiri・Huziwara→Fujiwara）。長音は在来の東方ローマ字の慣行どおり ou / uu をそのまま書く（Youmu・Yuuka・Joutouguu） |
| 5 | 出どころ欄: 公式（ZUN の対外表記・公式作品の英語）／慣用（英語圏の定訳）／当方（幻想蛮怒オリジナル等、ここで決めるもの） |
| 6 | HUD の桁が決まっている札（左の状態列 20 桁など）は短い形を使う。短形の個別決定は E5 の幅検査時に `messages.en.txt` の `W:` と一緒に行う（本表は正式な長い形を持つ） |
| 7 | 全角の『』・「」は英語では ASCII の `"` に、全角空白は半角に。読み仮名・ルビは落とす |

## 1. スペルカード・能力名の方針

| # | 方針 |
|---|---|
| 1 | 「〇符『タイトル』」は `〇 Sign "Title"`（公式の形式。恋符『マスタースパーク』 = `Love Sign "Master Spark"`） |
| 2 | 公式英題があるものはそれに合わせる（夢想封印 = Fantasy Seal・マスタースパーク = Master Spark・ミニ八卦炉 = Mini-Hakkero）。無いものは意訳 |
| 3 | 「〜する程度の能力」は "the ability to 〜" の形 |
| 4 | 固有の術語はローマ字＋慣用（弾幕 = danmaku・スペルカード = Spell Card・式神 = shikigami・妖怪 = youkai） |

## 2. 主要な術語

| 日本語 | 英語 | 出どころ |
|---|---|---|
| 幻想郷 | Gensokyo | 公式 |
| 異変 | incident | 公式 |
| 博麗大結界 | the Great Hakurei Barrier | 公式 |
| 結界 | barrier | 公式 |
| 結界ガード（magicmaster の防御） | Barrier Guard | 当方 |
| 弾幕 | danmaku | 公式 |
| スペルカード | Spell Card | 公式 |
| 外の世界 | the Outside World | 公式 |
| 里／人間の里 | the (Human) Village | 公式 |
| 妖怪 | youkai | 公式 |
| 妖精 | fairy | 公式 |
| 天狗 | tengu | 公式 |
| 河童 | kappa | 公式 |
| 鬼 | oni | 公式 |
| 巫女 | shrine maiden | 公式 |
| 式神 | shikigami | 公式 |
| 亡霊 | phantom | 慣用 |
| 幽霊 | ghost | 慣用 |
| 付喪神 | tsukumogami | 慣用 |
| 賽銭 | offering | 慣用 |
| 信仰 | faith | 慣用 |
| 尸解仙 | shikaisen | 慣用 |
| 山彦 | yamabiko | 慣用 |
| 山姥 | yamanba | 慣用 |
| あまのじゃく | amanojaku | 慣用 |
| 埴輪 | haniwa | 慣用 |
| 水子 | mizuko | 慣用 |
| 化け狸 | bake-danuki | 慣用 |

## 3. 町（`w_info.txt` の 15 件と基本の 1 件。元からある `W:E:` の英語名は変愚蛮怒の名残なので使いません）

| # | 日本語 | 英語 | 出どころ |
|---|---|---|---|
| 1 | 人里 | Human Village | 公式 |
| 2 | 天狗の里 | Tengu Village | 慣用 |
| 3 | 旧地獄街道 | Former Hell Road | 慣用（旧地獄 = Former Hell が公式） |
| 4 | 紅魔館 | Scarlet Devil Mansion | 公式 |
| 5 | 博麗神社 | Hakurei Shrine | 公式 |
| 6 | 河童のバザー | Kappa Bazaar | 当方 |
| 7 | 永遠亭 | Eientei | 公式 |
| 8 | 香霖堂 | Kourindou | 公式 |
| 9 | 命蓮寺 | Myouren Temple | 公式 |
| 10 | 霧雨魔法店 | Kirisame Magic Shop | 慣用 |
| 11 | 廃洋館 | Abandoned Western House | 当方（要確認: 元ネタがあれば寄せる） |
| 12 | 守矢神社 | Moriya Shrine | 公式 |
| 13 | 彼岸 | Higan | 公式 |
| 14 | 偽天棚 | False Tentana | 当方（要確認: 渡里ニナの偽の都市？ 読みが分からず仮にローマ字） |
| － | 街（WILDERNESS 無しの基本町） | Town | 現 E: のまま |

## 4. ダンジョン（`d_info.txt` の 17 件）

| # | 日本語 | いまの E: | 直し | 出どころ |
|---|---|---|---|---|
| 0 | 荒野 | Wilderness | ―（そのまま） | 在来 |
| 1 | 鉄獄 | Angband | ― | 在来 |
| 2 | 夢殿大祀廟の洞窟 | old shinrei byou | Cave of the Great Mausoleum | 公式（夢殿大祀廟 = Hall of Dreams' Great Mausoleum。長いので短縮） |
| 3 | 魔法の森深部 | Forest | Forest of Magic Depths | 公式（魔法の森 = Forest of Magic） |
| 4 | 玄武の沢 | genbu | Genbu Ravine | 公式 |
| 5 | 紅魔館深部 | Lakesidesugoiakaibuilding | Scarlet Devil Mansion Depths | 公式＋当方 |
| 6 | 地獄谷 | Mountain | Hell Valley | 当方 |
| 7 | 無縁塚 | Forest2 | Muenzuka | 公式 |
| 8 | 旧灼熱地獄 | Hell2 | Former Blazing Hell | 慣用（灼熱地獄跡 = Blazing Hell の変形） |
| 9 | 逆さ城 | Anti-magic cave | Inverted Castle | 当方（輝針城 = Shining Needle Castle の逆さ状態） |
| 10 | 仙界 | Anti-melee cave | Senkai | 公式 |
| 11 | 竜の住みか | Dragon's lair | ― | 在来 |
| 12 | 混沌の領域 | Lunar mare | Realm of Chaos | 当方 |
| 13 | 夢の世界 | Dream World | ― | 在来 |
| 14 | 地獄 | Hell | ― | 在来 |
| 15 | 虹龍洞 | Kouryuudou | Rainbow Dragon Cave | 慣用 |
| 16 | 浅間浄穢山 | Asamazyouesan | Mt. Asamajoue | 当方（幻想蛮怒オリジナル。要確認: 読み） |

## 5. 東方と幻想蛮怒追加のユニーク（r_info 1089〜1417。全 156 件）

欄は、番号 / 日本語 / 英語名（― は現在の E: のままでよいもの）/ 二つ名の英訳 / 出どころ です。
同名で番号が違うもの（段階ボス）は 1 行にまとめています。ID 1088 以前は変愚蛮怒に元からある
ものなので触りません（方針 3）。

| # | 日本語 | 英語名 | 二つ名 | 出 |
|---|---|---|---|---|
| 1089 | 宵闇の妖怪『ルーミア』 | ― (Rumia) | the Youkai of the Dusk | 公式 |
| 1090 | 氷の妖精『チルノ』 | ― (Cirno) | the Ice Fairy | 公式 |
| 1091 | 華人小娘『紅美鈴』 | Hong Meiling（現 Hong Meirin） | the Chinese Girl | 公式 |
| 1092 | 動かない大図書館『パチュリー・ノーレッジ』 | ― (Patchouli Knowledge) | the Unmoving Great Library | 公式 |
| 1093 | 紅魔館のメイド『十六夜　咲夜』 | ― (Sakuya Izayoi) | the Maid of the Scarlet Devil Mansion | 公式 |
| 1094 | 永遠に紅い幼き月『レミリア・スカーレット』 | ― (Remilia Scarlet) | the Eternally Young Scarlet Moon | 公式 |
| 1095/1221 | 悪魔の妹『フランドール・スカーレット』 | ― (Flandre Scarlet) | the Devil's Little Sister | 公式 |
| 1096 | 冬の忘れ物『レティ・ホワイトロック』 | ― (Letty Whiterock) | the Remnant of Winter | 公式 |
| 1097 | 凶兆の黒猫『橙』 | ― (Chen) | the Black Cat of Ill Omen | 公式 |
| 1098 | 七色の人形遣い『アリス・マーガトロイド』 | ― (Alice Margatroid) | the Seven-Colored Puppeteer | 公式 |
| 1099 | 騒霊ヴァイオリスト『ルナサ・プリズムリバー』 | ― (Lunasa Prismriver) | the Poltergeist Violinist | 公式 |
| 1100 | 騒霊トランペッター『メルラン・プリズムリバー』 | ― (Merlin Prismriver) | the Poltergeist Trumpeter | 公式 |
| 1101 | 騒霊キーボーディスト『リリカ・プリズムリバー』 | ― (Lyrica Prismriver) | the Poltergeist Keyboardist | 公式 |
| 1102 | 半人半霊の庭師『魂魄　妖夢』 | ― (Youmu Konpaku) | the Half-Human Half-Phantom Gardener | 公式 |
| 1103 | 華胥の亡霊『西行寺　幽々子』 | ― (Yuyuko Saigyouji) | the Dreaming Phantom | 公式 |
| 1104 | すきま妖怪の式『八雲　藍』 | ― (Ran Yakumo) | the Shikigami of the Gap Youkai | 公式 |
| 1105 | 神隠しの主犯『八雲　紫』 | ― (Yukari Yakumo) | the Mastermind of Spiritings-Away | 公式 |
| 1106 | 闇に蠢く光の蟲『リグル・ナイトバグ』 | ― (Wriggle Nightbug) | the Bug of Light Wriggling in the Dark | 公式 |
| 1107 | 夜雀の怪『ミスティア・ローレライ』 | ― (Mystia Lorelei) | the Night Sparrow | 公式 |
| 1108 | 知識と歴史の半獣『上白沢　慧音』 | ― (Keine Kamishirasawa) | the Half-Beast of Knowledge and History | 公式 |
| 1109 | 幸運の素兎『因幡　てゐ』 | ― (Tewi Inaba) | the Lucky White Rabbit | 公式 |
| 1110 | 狂気の月の兎『鈴仙・優曇華院・イナバ』 | ― (Reisen Udongein Inaba) | the Moon Rabbit of Insanity | 公式 |
| 1111 | 月の頭脳『八意　永琳』 | ― (Eirin Yagokoro) | the Brain of the Moon | 公式 |
| 1112/1293 | 永遠と須臾の罪人『蓬莱山　輝夜』 | ― (Kaguya Houraisan) | the Sinner of Eternity and the Instant | 公式 |
| 1113 | 蓬莱の人の形『藤原　妹紅』 | Fujiwara no Mokou（現 Huziwara） | the Figure of the Person of Hourai | 公式 |
| 1114 | 寂しさと終焉の象徴『秋　静葉』 | Shizuha Aki（現 Aki Shizuha） | the Symbol of Loneliness and Demise | 公式 |
| 1115 | 豊かさと稔りの象徴『秋　穣子』 | Minoriko Aki | the Symbol of Abundance and Harvest | 公式 |
| 1116 | 秘神流し雛『鍵山　雛』 | Hina Kagiyama | the Nagashi-bina of the Hidden God | 公式 |
| 1117 | 超妖怪弾頭『河城　にとり』 | Nitori Kawashiro | the Super Youkai Warhead | 公式 |
| 1118 | 下っ端哨戒天狗『犬走　椛』 | Momiji Inubashiri（現 Momizi） | the Rank-and-File Patrol Tengu | 公式 |
| 1119 | 伝統の幻想ブン屋『射命丸　文』 | Aya Shameimaru（現 Syameimaru） | the Traditional Reporter of Fantasy | 公式 |
| 1120 | 祀られる風の人間『東風谷　早苗』 | Sanae Kochiya（現 Kotiya） | the Enshrined Human of the Wind | 公式 |
| 1121 | 山坂と湖の権化『八坂　神奈子』 | Kanako Yasaka | the Avatar of Mountains and Lakes | 公式 |
| 1122 | 土着神の頂点『洩矢　諏訪子』 | Suwako Moriya | the Highest of the Native Gods | 公式 |
| 1123 | 春を運ぶ妖精『リリーホワイト』 | ― (Lily White) | the Fairy Who Heralds Spring | 公式 |
| 1124 | 恐るべき井戸の怪『キスメ』 | ― (Kisume) | the Fearsome Well Spirit | 公式 |
| 1125 | 暗い洞窟の明るい網『黒谷　ヤマメ』 | Yamame Kurodani | the Bright Net in the Dark Cave | 公式 |
| 1126 | 地殻の下の嫉妬心『水橋　パルスィ』 | Parsee Mizuhashi | the Jealousy Beneath the Earth's Crust | 公式 |
| 1127 | 語られる怪力乱神『星熊　勇儀』 | Yuugi Hoshiguma | the Rumored Unnatural Phenomenon | 公式 |
| 1128 | 怨霊も恐れ怯む少女『古明地　さとり』 | Satori Komeiji | the Girl Even Vengeful Spirits Fear | 公式 |
| 1129 | 地獄の輪禍『火焔猫　燐』 | Rin Kaenbyou | Hell's Traffic Accident | 公式 |
| 1130 | 熱かい悩む神の火『霊烏路　空』 | Utsuho Reiuji（現 Reiuzi） | the Scorching, Troubled Divine Flame | 公式 |
| 1131 | 閉じた恋の瞳『古明地　こいし』 | Koishi Komeiji | the Closed Eyes of Love | 公式 |
| 1132 | ダウザーの小さな大将『ナズーリン』 | ― (Nazrin) | the Little Dowser General | 公式 |
| 1133 | 愉快な忘れ傘『多々良　小傘』 | Kogasa Tatara | the Cheerful Forgotten Umbrella | 公式 |
| 1134 | 守り守られし大輪『雲居一輪＆雲山』 | Ichirin Kumoi & Unzan | the Guarding and Guarded Great Wheel | 公式 |
| 1135 | 水難事故の念縛霊『村紗　水蜜』 | Minamitsu Murasa（現 Captain Murasa） | the Ghost of the Shipwreck Accident | 公式 |
| 1136 | 毘沙門天の弟子『寅丸　星』 | Shou Toramaru（現 Syou） | the Disciple of Bishamonten | 公式 |
| 1137 | 封印された大魔法使い『聖　白蓮』 | Byakuren Hijiri（現 Hiziri） | the Sealed Great Magician | 公式 |
| 1138 | 未確認幻想飛行少女『封獣　ぬえ』 | Nue Houjuu | the Unidentified Fantastic Flying Girl | 公式 |
| 1139 | 読経するヤマビコ『幽谷　響子』 | Kyouko Kasodani | the Sutra-Chanting Yamabiko | 公式 |
| 1140 | 忠実な死体『宮古　芳香』 | Yoshika Miyako | the Loyal Corpse | 公式 |
| 1141 | 壁抜けの邪仙『霍　青娥』 | Seiga Kaku | the Wicked Hermit Who Passes Through Walls | 公式 |
| 1142 | 神の末裔の亡霊『蘇我　屠自古』 | Soga no Tojiko（現 Toziko） | the Phantom Descendant of the Gods | 公式 |
| 1143 | 古代日本の尸解仙『物部　布都』 | ― (Mononobe no Futo) | the Shikaisen of Ancient Japan | 公式 |
| 1144 | 聖徳道士『豊聡耳　神子』 | ― (Toyosatomimi no Miko) | the Shoutoku Taoist | 公式 |
| 1145 | 淡水に棲む人魚『わかさぎ姫』 | ― (Wakasagihime) | the Mermaid of Fresh Water | 公式 |
| 1146 | ろくろ首の怪奇『赤蛮奇』 | ― (Sekibanki) | the Rokurokubi Horror | 公式 |
| 1147 | 竹林のルーガルー『今泉　影狼』 | Kagerou Imaizumi | the Loup-Garou of the Bamboo Forest | 公式 |
| 1148 | 古びた琵琶の付喪神『九十九　弁々』 | Benben Tsukumo | the Tsukumogami of an Aged Biwa | 公式 |
| 1149 | 古びた琴の付喪神『九十九　八橋』 | Yatsuhashi Tsukumo | the Tsukumogami of an Aged Koto | 公式 |
| 1150/1224 | 逆襲のあまのじゃく『鬼人　正邪』 | Seija Kijin（現 Kijin Seija） | the Rebellious Amanojaku | 公式 |
| 1151 | 小人の末裔『少名　針妙丸』 | Shinmyoumaru Sukuna | the Descendant of the Inchlings | 公式 |
| 1152 | 夢幻のパーカッショニスト『堀川　雷鼓』 | Raiko Horikawa | the Phantasmal Percussionist | 公式 |
| 1153 | 片腕有角の仙人『茨木　華扇』 | Kasen Ibaraki | the One-Armed, Horned Hermit | 公式 |
| 1154 | 小さな百鬼夜行『伊吹　萃香』 | Suika Ibuki | the Tiny Night Parade of a Hundred Demons | 公式 |
| 1156 | 美しき緋の衣『永江　衣玖』 | ― (Iku Nagae) | the Beautiful Scarlet Cloth | 公式 |
| 1157 | 非想非非想天の娘『比那名居　天子』 | Tenshi Hinanawi | the Girl of Bhava-agra | 公式 |
| 1158 | 表情豊かなポーカーフェイス『秦　こころ』 | Hata no Kokoro（現 Hatano kokoro） | the Expressive Poker Face | 公式 |
| 1159 | 小さなスイートポイズン『メディスン・メランコリー』 | ― (Medicine Melancholy) | the Little Sweet Poison | 公式 |
| 1160 | 四季のフラワーマスター『風見　幽香』 | Yuuka Kazami（現 Kazami Yuka・末尾空白） | the Flower Master of the Four Seasons | 公式 |
| 1161 | 三途の水先案内人『小野塚　小町』 | Komachi Onozuka | the Ferryman of the Sanzu River | 公式 |
| 1162 | 楽園の最高裁判長『四季映姫・ヤマザナドゥ』 | Eiki Shiki, Yamaxanadu（現 Sikieiki） | the Supreme Judge of Paradise | 公式 |
| 1163 | 今どきの念写記者『姫海棠　はたて』 | Hatate Himekaidou | the Modern Spirit-Photography Reporter | 公式 |
| 1201 | 究極生命体『カーズ』 | Kars（現 ultimate thing） | the Ultimate Lifeform | 慣用（ジョジョ） |
| 1202 | 異端審問官『モズグス』 | Mozgus | the Inquisitor | 慣用（ベルセルク） |
| 1203 | 使徒？『モズグス』 | Mozgus（現 Mozgus2） | the Apostle? | 慣用 |
| 1204 | 空師『柳　龍光』 | Ryuukou Yanagi（現 頭に空白） | the Master of the Void Fist | 慣用（刃牙） |
| 1207 | UMA『チュパカブラ』 | ― (Chupacabra) | ― | 慣用 |
| 1212 | 『魔法の森の主』 | Master of the Forest of Magic（現 udongessyo-no-are） | ― | 当方 |
| 1226 | 博麗の巫女『博麗　霊夢』 | Reimu Hakurei（現 Hakurei reimu） | the Shrine Maiden of Hakurei | 公式 |
| 1227 | 普通の魔法使い『霧雨　魔理沙』 | Marisa Kirisame（現 Kirisame Marisa） | the Ordinary Magician | 公式 |
| 1230 | 化け狸十変化『二ッ岩　マミゾウ』 | Mamizou Futatsuiwa（現 Hutatsuiwa） | the Bake-danuki with Ten Transformations | 公式 |
| 1254 | UMA『モケーレ・ムベンベ』 | ― (Mokele-mbembe) | ― | 慣用 |
| 1256 | 『祐天上人』 | High Priest Yuuten（現 Yuuten-syounin） | ― | 当方 |
| 1257 | 『白仙』 | ― (Hakusen) | ― | 当方 |
| 1259 | 『万歳楽』 | Manzairaku（現 Tama） | ― | 当方 |
| 1261 | 『ニンジャスレイヤー』 | ― (Ninja Slayer) | ― | 慣用 |
| 1267/1322 | 秘封倶楽部初代会長『宇佐見　菫子』 | Sumireko Usami（現 Usami Sumireko） | the First President of the Secret Sealing Club | 公式 |
| 1268 | 『光の三妖精』 | Three Fairies of Light（現 3Fairies） | ― | 公式 |
| 1273 | 浅葱色のイーグルラヴィ『清蘭』 | ― (Seiran) | the Pale Blue Eagle Ravi | 公式 |
| 1274 | 橘色のイーグルラヴィ『鈴瑚』 | ― (Ringo) | the Orange Eagle Ravi | 公式 |
| 1275 | 夢の支配者『ドレミー・スイート』 | ― (Doremy Sweet) | the Ruler of Dreams | 公式 |
| 1276 | 舌禍をもたらす女神『稀神　サグメ』 | Sagume Kishin（現 Kishin Sagume） | the Goddess Whose Words Invite Calamity | 公式 |
| 1277 | 地獄の妖精『クラウンピース』 | ― (Clownpiece) | the Fairy of Hell | 公式 |
| 1278 | 『純狐』 | ― (Junko) | ― | 公式 |
| 1279-1281 | 地獄(異界/地球/月)の女神『ヘカーティア・ラピスラズリ』 | ― (Hecatia Lapislazuli) | the Goddess of Hell (Otherworld / Earth / Moon) | 公式 |
| 1291 | 『足売り婆』 | Ashiuri-baba（現 ashiuri） | the Leg-Selling Hag | 当方 |
| 1295 | 『八尺さま』 | Hasshaku-sama（現 Hassyaku-sama） | ― | 慣用 |
| 1297 | 神霊の依り憑く月の姫『綿月　依姫』 | Watatsuki no Yorihime（現 Yorihime） | the Lunar Princess Possessed by Divine Spirits | 公式 |
| 1298 | 海と山を繋ぐ月の姫『綿月　豊姫』 | Watatsuki no Toyohime（現 Toyohime） | the Lunar Princess Connecting Sea and Mountain | 公式 |
| 1299 | 『レイセン』 | ― (Reisen) | ―（2 羽目の月の兎） | 公式 |
| 1310 | 神に近づく蝶の妖精『エタニティラルバ』 | Eternity Larva（現 Etanity） | the Butterfly Fairy Approaching God | 公式 |
| 1311 | 浮世の関を超える山姥『坂田　ネムノ』 | Nemuno Sakata | the Yamanba Beyond the Barriers of the Mundane World | 公式 |
| 1312 | 神仏に心酔する守護神獣『高麗野　あうん』 | Aunn Komano | the Guardian Beast Devoted to Gods and Buddhas | 公式 |
| 1313 | 森で垂迹した魔法地蔵『矢田寺　成美』 | Narumi Yatadera | the Magic Jizou Manifested in the Forest | 公式 |
| 1314 | 危険すぎるバックダンサー『爾子田　里乃』 | Satono Nishida | the Far Too Dangerous Backup Dancer | 公式 |
| 1315 | 危険すぎるバックダンサー『丁礼田　舞』 | Mai Teireida | the Far Too Dangerous Backup Dancer | 公式 |
| 1316 | 究極の絶対秘神『摩多羅　隠岐奈』 | Okina Matara | the Ultimate, Absolute Hidden God | 公式 |
| 1318 | 判読眼のビブロフィリア『本居　小鈴』 | Kosuzu Motoori | the Bibliophile with a Deciphering Eye | 公式 |
| 1319 | 最凶最悪の双子の妹『依神　女苑』 | Joon Yorigami（現 Jyoon・末尾空白） | the Younger of the Most Wicked Twins | 公式 |
| 1320/1321 | 最凶最悪の双子の姉『依神　紫苑』 | Shion Yorigami（現 末尾空白） | the Elder of the Most Wicked Twins | 公式 |
| 1341 | 河原のアイドル水子『戎　瓔花』 | Eika Ebisu（現 Ebisu Eika） | the Idol Mizuko of the Riverbed | 公式 |
| 1342 | 古代魚の子連れ番人『牛崎　潤美』 | Urumi Ushizaki | the Child-Toting Keeper of Ancient Fish | 公式 |
| 1343 | 地獄関所の番頭神『庭渡　久侘歌』 | Kutaka Niwatari | the God Managing Hell's Checkpoint | 公式 |
| 1344 | 鬼傑組組長『吉弔　八千慧』 | Yachie Kicchou（現 Kitcho） | the Boss of the Kiketsu Family | 公式 |
| 1345 | 埴輪兵長『杖刀偶　磨弓』 | Mayumi Joutouguu（現 Joutougu） | the Haniwa Corporal | 公式 |
| 1346 | 孤立無援が誂えた造形神『埴安神　袿姫』 | Keiki Haniyasushin | the Sculptor God for the Isolated | 公式 |
| 1347 | 勁牙組組長『驪駒　早鬼』 | Saki Kurokoma | the Boss of the Keiga Family | 公式 |
| 1348 | 『ライカ』 | ― (Laika) | ― | 慣用 |
| 1355 | 夢幻酒場『鯢呑亭』の看板娘『奥野田　美宵』 | Miyoi Okunoda | the Poster Girl of the Geidontei | 公式 |
| 1357 | 商売繁盛の縁起物『豪徳寺　ミケ』 | Mike Goutokuji（現 Goutokuzi） | the Lucky Charm of Prosperous Trade | 公式 |
| 1358 | 山奥のビジネス妖怪『山城　たかね』 | Takane Yamashiro | the Business Youkai of the Mountain Recesses | 公式 |
| 1359 | 高地に棲む山女郎『駒草　山如』 | Sannyo Komakusa | the Yamajorou of the Highlands | 公式 |
| 1360 | 本物の勾玉制作職人『玉造　魅須丸』 | Misumaru Tamatsukuri | the Genuine Magatama Craftsman | 公式 |
| 1361 | 耳元で囁く邪悪な白狐『菅牧　典』 | Tsukasa Kudamaki | the Wicked White Fox Whispering in Your Ear | 公式 |
| 1362 | 鴉天狗の大将『飯綱丸　龍』 | Megumu Iizunamaru | the General of the Crow Tengu | 公式 |
| 1363 | 無主物の神『天弓　千亦』 | Chimata Tenkyuu（現 Tenkyu） | the God of Unclaimed Goods | 公式 |
| 1364 | 黒きドラゴンイーター『姫虫　百々世』 | Momoyo Himemushi | the Black Dragon-Eater | 公式 |
| 1366 | 無敗の剛欲同盟長『饕餮　尤魔』 | Yuuma Toutetsu（現 Toutetsu Yuma） | the Undefeated Head of the Gouyoku Alliance | 公式 |
| 1368 | 爪弾きにされた反獄の怨霊『宮出口　瑞霊』 | Mizuchi Miyadeguchi（現 小文字） | the Ostracized, Prison-Defying Vengeful Spirit | 公式 |
| 1385 | 森閑のケルベロス『三頭　慧ノ子』 | Enoko Mitsugashira | the Cerberus of the Still Forest | 公式 |
| 1386 | 小さな聖域の大聖『孫　美天』 | Son Biten（現 Son biten。中華姓は姓先） | the Great Sage of the Small Sanctuary | 公式 |
| 1387 | 穢れた有機物の怪物『天火人　ちやり』 | Chiyari Tenkajin（現 小文字） | the Monster of Filthy Organic Matter | 公式 |
| 1388 | 地獄の美しきストーカー『豫母都　日狭美』 | Hisami Yomotsu（現 小文字） | the Beautiful Stalker of Hell | 公式 |
| 1389 | 寂滅為楽の王『日白　残無』 | Zanmu Nippaku（現 Nippaku Zanmu） | the King of Blissful Nirvana | 公式 |
| 1391 | 『脱獄した動物霊(鬼傑組)』 | Escaped Animal Spirit (Kiketsu) | ― | 当方 |
| 1408 | 聖域に棲む山姥の長『塵塚　ウバメ』 | Ubame Chirizuka | the Chief of the Yamanba in the Sanctuary | 当方（幻想蛮怒オリジナル） |
| 1409 | 正体不明の順わぬ妖獣『封獣　チミ』 | Chimi Houjuu（現 Houju） | the Unidentified, Untamed Youkai Beast | 当方 |
| 1410 | 逼塞した聖地の道祖神『道神　馴子』 | Nareko Michigami | the Wayside God of the Secluded Holy Land | 当方 |
| 1411 | 錦上の京の急所『ユイマン・浅間』 | Yuiman Asama | the Vital Point of the Brocade Capital | 当方 |
| 1412 | 恒久の姫『磐永　阿梨夜』 | Ariya Iwanaga | the Princess of Permanence | 当方 |
| 1413 | 虚構の都市を創る怪『渡里　ニナ』 | Nina Watari | the Phantom Who Creates Fictitious Cities | 当方 |

変えないもの（現在の E: のままで正しいもの）: 1089 Rumia、1090 Cirno、1092〜1112 の欧米風の名前、
1143 / 1144 / 1158 の「の」を含む名前、1145 Wakasagihime、1146 Sekibanki、1159 Medicine、
1207 / 1254 UMA、1257 Hakusen、1261 Ninja Slayer、1273〜1281 の月の一族、1299 Reisen、1348 Laika。
1088 三上山の大ムカデ（Great Centipede of Mt. Mikamisan）もそのままです。

## 6. クエスト・町に出る人物（r_info に居ない者）

| 日本語 | 英語 | 出どころ |
|---|---|---|
| 稗田　阿求（阿求） | Hieda no Akyuu (Akyuu) | 公式 |
| 森近　霖之助 | Rinnosuke Morichika | 公式 |
| 鯢呑亭 | the Geidontei | 公式 |

店主と建物の主人は `town.en.txt` で扱い、この表の人物と一致するものはこの表に従います。
それ以外（幻想蛮怒オリジナルの店主）はローマ字にします（§0 方針 4）。

## 7. ランダムユニークの名簿（`ru_name*` 3 本）

ローマ字にします（2026-08-24 に決定）。名前の断片（接頭語、女性名、妖怪名）をヘボン式で
音写し、英語名を新しく作ることはしません。
