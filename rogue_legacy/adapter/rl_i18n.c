#include "rl_i18n.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int rl_decode_ansi_char(const unsigned char *p, unsigned int *codepoint)
{
    wchar_t wide[2];
    int length;
    int count;
    if (p == NULL || *p == '\0' || codepoint == NULL) return 0;
    length = (IsDBCSLeadByteEx(CP_ACP, p[0]) && p[1] != '\0') ? 2 : 1;
    count = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, (const char *)p, length, wide, 2);
    if (count != 1) return 0;
    *codepoint = (unsigned int)wide[0];
    return length;
}
#else
int rl_decode_ansi_char(const unsigned char *p, unsigned int *codepoint)
{
    (void)p;
    (void)codepoint;
    return 0;
}
#endif

typedef struct rl_translation {
    const char *en;
    const char *ja;
} rl_translation;

/*
 * Display-only localization.
 *
 * Legacy Rogue keeps all game state and save data in the original English
 * representation.  Translation happens only when text crosses the virtual
 * curses boundary, so UTF-8 never expands the old 80-byte game buffers and the
 * original save formats remain untouched.
 */
static const rl_translation kTranslations[] = {
    { "--More--", "［続く］" },
    { "Direction?", "方向は？" },
    { "Which direction?", "方向は？" },
    { "Press space to continue", "スペースで続ける" },
    { "--Press space to continue--", "［スペースで続ける］" },
    { "[Press return to continue]", "［Enterで続ける］" },
    { "[hit RETURN to continue]", "［Enterで続ける］" },
    { "Hungry", "空腹" },
    { "Weak", "飢餓" },
    { "Faint", "瀕死" },
    { "Fainting", "瀕死" },
    { "Gold", "金塊" },
    { "Level", "階" },
    { "Hp", "体力" },
    { "Str", "強さ" },
    { "Arm", "守備" },
    { "Ac", "守備" },
    { "Exp", "経験" },
    { "True", "有効" },
    { "False", "無効" },
    { "Overwrite", "上書き" },
    { "Slow", "1行ずつ" },
    { "Clear", "画面消去" },
    { "What do you want to call it?", "何と呼びますか？" },
    { "What do you want to call him?", "何と呼びますか？" },
    { "Call it: ", "呼び名: " },
    { "Call it:", "呼び名: " },
    { "Really quit?", "本当に終了しますか？" },
    { "Really quit? [y/n]", "本当に終了しますか？ [y/n]" },
    { "Character you want help for (* for all): ", "調べるコマンドは？（*ですべて）: " },
    { "character you want help for (* for all): ", "調べるコマンドは？（*ですべて）: " },
    { "What do you want identified? ", "調べる記号は？ " },
    { "what do you want identified? ", "調べる記号は？ " },
    { "Left or Right ring? ", "左手と右手のどちら？ " },
    { "left or right ring? ", "左手と右手のどちら？ " },
    { "Left hand or right hand? ", "左手と右手のどちら？ " },
    { "left hand or right hand? ", "左手と右手のどちら？ " },
    { "L or R", "L（左）かR（右）を選んでください。" },
    { "Please type L or R", "L（左）かR（右）を選んでください。" },
    { "please type L or R", "L（左）かR（右）を選んでください。" },
    { "Save file: ", "セーブファイル: " },
    { "File name: ", "ファイル名: " },
    { "Name: ", "名前: " },
    { "Fruit: ", "好きな果物: " },
    { "Welcome to level %d", "レベル%dへようこそ。" },
    { "Welcome to level %d.", "レベル%dへようこそ。" },
    { "You are starting to feel weak.", "空腹で力がなくなってきた。" },
    { "You are starting to get hungry.", "空腹になってきた。" },
    { "You faint from lack of food.", "空腹で目がくらんだ。" },
    { "You can move again.", "ようやく体が自由になった。" },
    { "You can move again", "ようやく体が自由になった。" },
    { "You can't go down here.", "ここからは降りられない。" },
    { "You can't go up here.", "ここからは登れない。" },
    { "There is no way down.", "降りる階段がない。" },
    { "There is no way up.", "登る階段がない。" },
    { "I see no way down", "降りる階段がない。" },
    { "I see no way down.", "降りる階段がない。" },
    { "I see no way up", "登る階段がない。" },
    { "I see no way up.", "登る階段がない。" },
    { "You have no food", "食べ物を持っていない。" },
    { "You have nothing to drop.", "落とす物を持っていない。" },
    { "You have nothing to drop", "落とす物を持っていない。" },
    { "You aren't wearing any armor", "よろいを着ていない。" },
    { "You are already wearing some armor", "すでによろいを着ている。" },
    { "You have no rings on", "指輪をはめていない。" },
    { "Which hand?", "どちらの手？" },
    { "Quaff what?", "飲む水薬は？" },
    { "Read what?", "読む巻き物は？" },
    { "Eat what?", "食べる物は？" },
    { "Drop what?", "落とす物は？" },
    { "Wear what?", "着るよろいは？" },
    { "Wield what?", "持つ武器は？" },
    { "Zap with what?", "振るつえは？" },
    { "Throw what?", "投げる物は？" },
    { "Identify what?", "調べる持ち物は？" },
    { "Inventory what?", "調べる持ち物は？" },
    { "Your pack is empty", "持ちものは、1つも持っていない。" },
    { "No such item.", "それは持っていない。" },
    { "Nothing appropriate", "該当する物を持っていない。" },
    { "Nothing discovered", "知っている物は、まだない。" },
    { "Pack full", "これ以上は物を持てない。" },
    { "Pack too full", "もうこれ以上、物は持てない。" },
    { "There's already something there", "そこには、すでに何かが置かれている。" },
    { "Not wearing anything", "よろいを着ていない。" },
    { "Not wielding anything", "武器は使っていない。" },
    { "You're already wearing some", "ほかのよろいを着ている。" },
    { "You can't wear that", "それはよろいではない。" },
    { "In use", "その武器はもう使っている。" },
    { "Call what?", "呼び名をつける物は？" },
    { "Surely you already know what that's called", "それには呼び名をつける必要がない。" },
    { "Nothing here", "ここには何もない。" },
    { "Unknown command", "そのコマンドは使えない。" },
    { "Nothing happens", "しかし、何も起こらなかった。" },
    { "You are being held", "怪物につかまえられて、逃げられない。" },
    { "You are caught in a bear trap", "熊のわなにつかまった！" },
    { "You are still stuck in the bear trap", "熊のわなが、なかなかはずれない。" },
    { "You faint", "空腹で目がくらくらする。" },
    { "You feel less confused now", "ようやく頭がはっきりしてきた。" },
    { "The veil of darkness lifts", "暗闇のとばりが上がってゆく。" },
    { "You feel yourself slowing down", "素早くなる効果がなくなった。" },
    { "You float gently to the ground", "ようやく地面に足がついた。" },
    { "Your armor weakens", "よろいはさびてしまった！" },
    { "Your purse feels lighter", "金塊を盗まれた！" },
    { "Yum, that tasted good", "ああ、おいしかった。" },
    { "A small dart just hit you in the shoulder", "小さな投げ矢が飛んできて、肩にささった！" },
    { "Wait, what's going on here. Huh? What? Who?", "あれ？ ここはどこ？ 私はだれ？" },
    { "The scroll turns to dust as you pick it up", "拾いあげたとたん、巻き物はちりになってしまった。" },
    { "You can't, it appears to be cursed", "呪われているので、それはできない！" },
    { "Shell escape is not available in HD2D.", "HD2Dではシェル呼び出しは使えません。" },
    { "Shell escape is not available in HD2D", "HD2Dではシェル呼び出しは使えません。" },
    { "Not wearing armor", "よろいを着ていない。" },
    { "You feel a wrenching sensation in your gut.", "腹の底がねじれるような感覚がした。" },
    { "You feel too weak from lack of food.  You faint", "空腹で力が入らず、気を失った。" },
    { "bummer!  You've hit the ground", "しまった！ 地面に降りてしまった。" },
    { "You can't call that anything", "それには呼び名を付けられない。" },
    { "That has already been identified", "それはすでに識別済みだ。" },
    { "Getting hungry", "腹が減ってきた。" },
    { "Wait! That's a mimic!", "待て！ それはミミックだ！" },
    { "Your hands stop glowing red", "手の赤い輝きが消えた。" },
    { "Your armor appears to be weaker now. Oh my!", "よろいが弱くなってしまった！" },
    { "You feel a sting in your arm and now feel weaker", "腕を刺され、力が抜けていく。" },
    { "A sting has weakened you", "刺し傷で力が弱くなった。" },
    { "A sting momentarily weakens you", "刺し傷で一時的に力が弱くなった。" },
    { "Sting has no effect", "刺し傷は何の効果もなかった。" },
    { "You suddenly feel weaker.", "急に力が弱くなった。" },
    { "Ugh, you would get ill if you ate that.", "うっ、それを食べたら具合が悪くなりそうだ。" },
    { "That's Inedible!", "それは食べられない！" },
    { "Yuk, this food tastes awful", "うえっ、この食べ物はひどい味だ。" },
    { "You faint from exhaustion.", "疲労で気を失った。" },
    { "The umber hulk's gaze has confused you.", "アンバーハルクの視線で混乱した。" },
    { "You fell into a trap!", "わなに落ちた！" },
    { "A strange white mist envelops you and you fall asleep", "奇妙な白い霧に包まれ、眠り込んだ。" },
    { "Oh no! An arrow shot you", "しまった！ 矢が命中した。" },
    { "The arrow killed you.", "矢に倒された。" },
    { "An arrow shoots past you.", "矢がすぐそばを飛び過ぎた。" },
    { "The dart killed you.", "投げ矢に倒された。" },
    { "A small dart whizzes by your ear and vanishes.", "小さな投げ矢が耳元をかすめて消えた。" },
    { "You can't carry anything else.", "これ以上は物を持てない。" },
    { "You aren't carrying anything", "何も持っていない。" },
    { "You aren't carrying anything.", "何も持っていない。" },
    { "Warning, connectivity problem on this level.", "警告: この階の通路接続に問題がある。" },
    { "Yuk! Why would you want to drink that?", "うえっ！ それは飲めない。" },
    { "That's undrinkable", "それは飲めない。" },
    { "You feel very sick now.", "ひどく気分が悪くなった。" },
    { "You feel momentarily sick", "一瞬、気分が悪くなった。" },
    { "You begin to feel better.", "少し気分が良くなってきた。" },
    { "You feel stronger, now.  What bulging muscles!", "力がみなぎってきた！" },
    { "You have a strange feeling for a moment, then it passes.", "一瞬、奇妙な感覚がしたが、すぐ消えた。" },
    { "You can't move.", "体が動かない。" },
    { "You suddenly feel much more skillful", "急にずっと身軽になった気がする。" },
    { "You begin to feel much better.", "かなり気分が良くなってきた。" },
    { "You feel yourself moving much faster.", "体がずっと素早く動くようになった。" },
    { "Hey, this tastes great.  It make you feel warm all over.", "これはうまい。体中が暖かくなった。" },
    { "A cloak of darkness falls around you.", "闇のとばりが周囲を覆った。" },
    { "This potion tastes extremely dull.", "この水薬はひどく味気ない。" },
    { "What an odd tasting potion!", "なんとも妙な味の水薬だ！" },
    { "It would be difficult to wrap that around a finger", "それを指にはめるのは無理だ。" },
    { "Not a ring", "それは指輪ではない。" },
    { "You already have a ring on each hand", "両手にはすでに指輪をはめている。" },
    { "Wearing two", "指輪を2つはめている。" },
    { "No rings", "指輪をはめていない。" },
    { "You aren't wearing any rings", "指輪をはめていない。" },
    { "Not wearing such a ring", "その指輪ははめていない。" },
    { "Save game failed!", "セーブに失敗した！" },
    { "There is nothing on it to read", "そこには読めるものがない。" },
    { "Nothing to read", "読めるものがない。" },
    { "As you read the scroll, it vanishes.", "巻き物を読むと消えてしまった。" },
    { "Your hands begin to glow red", "手が赤く輝き始めた。" },
    { "The corridor glows and then fades", "通路が一瞬輝き、すぐ元に戻った。" },
    { "The room is lit", "部屋が明るくなった。" },
    { "Your armor glows faintly for a moment", "よろいが一瞬かすかに輝いた。" },
    { "You fall asleep.", "眠り込んでしまった。" },
    { "You hear a faint cry of anguish in the distance.", "遠くからかすかな苦鳴が聞こえた。" },
    { "This scroll is an identify scroll", "これは識別の巻き物だ。" },
    { "Oh, now this scroll has a map on it.", "巻き物に地図が浮かび上がった。" },
    { "You begin to feel a pull downward", "下へ引かれるような感覚がした。" },
    { "You feel a strange sense of loss.", "何か大切なものを失ったような気がした。" },
    { "You hear maniacal laughter in the distance.", "遠くから狂った笑い声が聞こえた。" },
    { "You feel as if somebody is watching over you.", "誰かに見守られているような気がする。" },
    { "You hear a high pitched humming noise.", "甲高いうなり音が聞こえた。" },
    { "This scroll seems to be blank.", "この巻き物は白紙のようだ。" },
    { "You have been granted the boon of genocide", "怪物を消し去る力を授かった。" },
    { "What a puzzling scroll!", "なんとも不可思議な巻き物だ！" },
    { "You can't zap with that!", "それでは魔法を使えない！" },
    { "You are too weak to use it.", "力が弱すぎて使えない。" },
    { "Missile vanishes", "魔法の弾が消えた。" },
    { "The missile vanishes with a puff of smoke", "魔法の弾が煙とともに消えた。" },
    { "The bolt bounces", "光線が跳ね返った。" },
    { "What a bizarre schtick!", "なんとも奇妙なつえだ！" },
    { "You have a tingling feeling", "体がぴりぴりする。" },
    { "That gold must have been counterfeit", "その金塊は偽物だったようだ。" },
    { "There is something there already", "そこにはすでに何かがある。" },
    { "You can't.  It appears to be cursed.", "呪われているので、それはできない。" },
    { "There is nothing here to pick up", "ここには拾える物がない。" },
    { "You haven't typed a command yet", "まだ繰り返すコマンドがない。" },
    { "No monster there", "そこには怪物がいない。" },
    { "No trap there", "そこにはわながない。" },
    { "A secret door", "隠し扉" },
    { "Your way is magically blocked", "魔法の力で行く手を阻まれた。" },
    { "You can't.  You're floating off the ground!", "空中に浮いているので、それはできない！" },
    { "Everything looks SO boring now.", "すべてが急につまらなく見えてきた。" },
    { "You are suddenly in a parallel dimension", "突然、別の次元へ飛ばされた。" },
    { "You feel a sting in the side of your neck", "首筋に鋭い痛みを感じた。" },
    { "Multi-colored lines swirl around you, then fade", "色とりどりの線が周囲を渦巻き、消えていった。" },
    { "A spike shoots past your ear!", "とげが耳元を飛び過ぎた！" },
    { "You suddenly feel very thirsty", "急にひどく喉が渇いた。" },
    { "You feel time speed up suddenly", "急に時間の流れが速くなった。" },
    { "Time now seems to be going slower", "時間の流れが遅くなったようだ。" },
    { "An arrow killed you", "矢に倒された。" },
    { "An arrow shoots past you", "矢がすぐそばを飛び過ぎた。" },
    { "A poisoned dart killed you", "毒矢に倒された。" },
    { "A small dart just hit you in the shoulder", "小さな投げ矢が肩に刺さった！" },
    { "A gush of water hits you on the head", "水が頭上から勢いよく降りかかった。" },
    { "The rust vanishes instantly", "さびがたちまち消えた。" },
    { "You ran out", "もう持っていない。" },
    { "Please answer Y or N", "YかNで答えてください。" },
    { "File exists.  Do you wish to overwrite it?", "ファイルはすでに存在します。上書きしますか？" },
    { "Your nose tingles", "鼻がむずむずする。" },
    { "The flame bounces", "炎が跳ね返った。" },
    { "Off the dragon", "ドラゴンに跳ね返された。" },
    { "Not a type", "その種類はない。" },

    /* Identify-symbol names. */
    { "wall of a room", "部屋の壁" },
    { "gold", "金塊" },
    { "passage leading down", "下の階への階段" },
    { "a staircase", "隣の階への階段" },
    { "door", "部屋の扉" },
    { "room floor", "部屋の床" },
    { "you", "あなた" },
    { "passage", "通路" },
    { "trap", "わな" },
    { "potion", "水薬" },
    { "scroll", "巻き物" },
    { "food", "食糧" },
    { "weapon", "武器" },
    { "solid rock", "岩壁" },
    { "armor", "よろい" },
    { "The Amulet of Yendor", "イェンダーの魔除け" },
    { "the Amulet of Yendor", "イェンダーの魔除け" },
    { "ring", "指輪" },
    { "wand or staff", "魔法のつえ" },
    { "unknown character", "不明な文字" },

    /* Screens and messages found by tools/rogue_legacy/i18n_scan.py (2026-09-23). */
    { "Which item do you wish to inventory: ", "どの持ち物を調べますか: " },
    { "Item: ", "持ち物: " },
    { "Empty handed.", "何も持っていない。" },
    { "You are empty handed.", "何も持っていない。" },
    { "empty handed", "何も持っていない。" },
    { "you are empty handed", "何も持っていない。" },
    { "In use.", "それはもう使っている。" },
    { "That's already in use.", "それはもう使っている。" },
    { "Nothing happens.", "しかし、何も起こらなかった。" },
    { "I see no monster there", "そこにモンスターはいない。" },
    { "you don't have anything in your pack to identify", "調べられる物を持っていない。" },
    { "you can't.  It appears to be cursed", "できない。呪われているようだ。" },
    { "you are too weak to use it", "力が足りなくて使えない。" },
    { "moved onto %s", "%sの上へ移動した。" },
    { "%d gold pieces", "%d個の金塊" },
    { "%d gold pieces.", "%d個の金塊。" },
    { "You quit with %d gold pieces", "%d個の金塊を持って引き上げた。" },
    { "  Hungry", "  空腹" },
    { "  Weak", "  飢餓" },
    { "  Fainting", "  瀕死" },
    { "you are getting the munchies", "お腹が鳴りはじめた。" },
    { "getting the munchies", "お腹が鳴りはじめた。" },
    { "the munchies are interfering with your motor capabilites", "空腹で体が思うように動かない。" },
    { "you faint from exhaustion", "力尽きて気を失った。" },
    { "you feel yourself slowing down.", "体の動きが鈍ってきた。" },
    { "You feel yourself slowing down.", "体の動きが鈍ってきた。" },
    { "you feel a wrenching sensation in your gut", "腹の底がねじれるような感じがした。" },
    { "you feel very sick now", "ひどく気分が悪くなった。" },
    { "you feel momentarily sick", "少し気分が悪くなった。" },
    { "you begin to feel better", "気分がよくなってきた。" },
    { "you begin to feel much better", "ずいぶん気分がよくなった。" },
    { "you feel yourself moving much faster", "体がずっと速く動くようになった。" },
    { "you feel stronger, now.  What bulging muscles!", "力がみなぎってきた。筋肉が盛り上がっている。" },
    { "hey, this tastes great.  It make you feel warm all over", "とてもおいしい。体の芯から温まってきた。" },
    { "you feel very sick", "ひどく気分が悪い。" },
    { "you suddenly feel weaker", "急に力が抜けた。" },
    { "you feel a bite in your leg and now feel weaker", "足をかまれて力が抜けた。" },
    { "a bite has weakened you", "かまれて力が抜けた。" },
    { "a bite momentarily weakens you", "かまれて一瞬力が抜けた。" },
    { "bite has no effect", "かまれたが、何ともなかった。" },
    { "you feel a strange sense of loss", "何かを失ったような気がした。" },
    { "you fall asleep", "眠り込んでしまった。" },
    { "you hear a faint cry of anguish in the distance", "遠くでかすかな苦悶の叫びが聞こえた。" },
    { "you hear maniacal laughter in the distance", "遠くで狂ったような笑い声が聞こえた。" },
    { "you feel in touch with the Universal Onenes", "宇宙との一体感に包まれた。" },
    { "you hear a high pitched humming noise", "甲高いうなりが聞こえた。" },
    { "missle vanishes", "矢は消えた。" },
    { "the missle vanishes with a puff of smoke", "矢は煙とともに消えた。" },
    { "far out!  Everything is all cosmic again", "ようやく頭がはっきりしてきた。" },
    { "trippy", "幻覚" },
    { "confused", "混乱" },
    { "You freak out", "正気を失った。" },
    { "Ok, if you want to exit that badly, I'll have to allow it", "そこまで言うなら、終わりにしよう。" },
    { "Sorry.  You took to long.  You are dead", "時間をかけすぎました。あなたは死にました。" },
    { "     Congratulations, you have made it to the light of day!    ", "            おめでとう。あなたは地上の光を取り戻した。            " },
    { "You have joined the elite ranks of those who have escaped the", "あなたは運命の洞窟から生きて帰った、ひと握りの冒険者の" },
    { "Dungeons of Doom alive.  You journey home and sell all your loot at", "仲間入りを果たした。持ち帰った戦利品は高値で売れ、あなたは" },
    { "a great profit and are admitted to the fighters guild.", "戦士の組合に迎え入れられた。" },
    { "a great profit and are admitted to the Fighters' Guild.", "戦士の組合に迎え入れられた。" },
    { "   Worth  Item", "     価値  持ち物" },
    { "   %5d  Gold Pieces          ", "   %5d  個の金塊          " },
    { "   %5d  Gold Peices          ", "   %5d  個の金塊          " },
    { "Killed by ", "倒したのは " },
    { "Yes", "はい" },
    { "(T or F)", "（T か F）" },
    { "(O, S, or C)", "（O か S か C）" },
    { "Please type one of %c%c%c%c (ESCAPE to quit)", "%c%c%c%c のどれかを押してください（エスケープでやめる）" },
    { "For what type of object do you want a list? (* for all)", "どの種類の物の一覧を見ますか？（* ですべて）" },
    { "Ugh, you would get ill if you ate that", "うえっ、こんな物を食べたらお腹をこわす。" },
    { "ugh, you would get ill if you ate that", "うえっ、こんな物を食べたらお腹をこわす。" },
    { "Top Ten Rogueists:", "上位 10 人のローグ:" },
    { "Top Ten Scores:", "上位 10 件の得点:" },
    { "   Score Name", "   得点 名前" },
    { "Which item do you wish to inventory:", "どの持ち物を調べますか:" },
    /* Help screen (3.6 init.c / 5.4 extern.c helpstr[]) and score screens. */
    { "\trun left until adjacent", "\t何かに出会うまで左へ連続移動" },
    { "\trun down until adjacent", "\t何かに出会うまで下へ連続移動" },
    { "\trun up until adjacent", "\t何かに出会うまで上へ連続移動" },
    { "\trun right until adjacent", "\t何かに出会うまで右へ連続移動" },
    { "\trun up & left until adjacent", "\t何かに出会うまで左上へ連続移動" },
    { "\trun up & right until adjacent", "\t何かに出会うまで右上へ連続移動" },
    { "\trun down & left until adjacent", "\t何かに出会うまで左下へ連続移動" },
    { "\trun down & right until adjacent", "\t何かに出会うまで右下へ連続移動" },
    { "Top Ten Adventurers:", "上位 10 人の冒険者:" },
    { "Rank\tScore\tName", "順位\t得点\t名前" },
    { "Sorry.  You took too long.  You are dead", "時間をかけすぎました。あなたは死にました。" },
    { "Okay, bye bye!", "それでは、さようなら。" },
    /* Option screens. */
    { "Terse output: ", "簡潔なメッセージ: " },
    { "Flush typeahead during battle: ", "戦闘中に先行入力を破棄: " },
    { "Show position only at end of run: ", "連続移動の終点だけ表示: " },
    { "Do inventories one line at a time: ", "持ち物を1行ずつ表示: " },
    { "Ask me about unidentified things: ", "未識別品を拾う前に確認: " },
    { "Terse output", "簡潔なメッセージ" },
    { "Flush typeahead during battle", "戦闘中に先行入力を破棄" },
    { "Show position only at end of run", "連続移動の終点だけ表示" },
    { "Show the lamp-illuminated floor", "明かりの届く床を表示" },
    { "Follow turnings in passageways", "通路の曲がり角を自動追従" },
    { "Print out tombstone when killed", "死亡時に墓碑を表示" },
    { "Inventory style", "持ち物表示方式" },
    { "Name", "名前" },
    { "Fruit", "好きな果物" },
    { "Save file", "セーブファイル" },

    /* Help descriptions shared by both generations. */
    { "\tprints help", "\tヘルプを表示" },
    { "\tidentify object", "\t画面の記号を調べる" },
    { "\tleft", "\t左へ移動" },
    { "\tdown", "\t下へ移動" },
    { "\tup", "\t上へ移動" },
    { "\tright", "\t右へ移動" },
    { "\tup & left", "\t左上へ移動" },
    { "\tup & right", "\t右上へ移動" },
    { "\tdown & left", "\t左下へ移動" },
    { "\tdown & right", "\t右下へ移動" },
    { "\trun left", "\t左へ連続移動" },
    { "\trun down", "\t下へ連続移動" },
    { "\trun up", "\t上へ連続移動" },
    { "\trun right", "\t右へ連続移動" },
    { "\trun up & left", "\t左上へ連続移動" },
    { "\trun up & right", "\t右上へ連続移動" },
    { "\trun down & left", "\t左下へ連続移動" },
    { "\trun down & right", "\t右下へ連続移動" },
    { "\t<SHIFT><dir>: run that way", "\t<SHIFT><方向>: その方向へ連続移動" },
    { "\t<CTRL><dir>: run till adjacent", "\t<CTRL><方向>: 何かを見つけるまで移動" },
    { "<dir>\tfight till death or near death", "<方向>\t危険になるまで連続戦闘" },
    { "<dir>\tfight till either of you dies", "<方向>\tどちらかが倒れるまで戦う" },
    { "<dir>\tthrow something", "<方向>\t物を投げる" },
    { "<dir>\tmove onto without picking up", "<方向>\t拾わずにそのマスへ移動" },
    { "<dir>\tzap a wand in a direction", "<方向>\tつえを振る" },
    { "<dir>\tidentify trap type", "<方向>\tわなの種類を調べる" },
    { "<dir>\tforward until find something", "<方向>\t何かを見つけるまで進む" },
    { "\tzap a wand or staff", "\tつえを振る" },
    { "\tsearch for trap/secret door", "\tわなや隠し扉を探す" },
    { "\tgo down a staircase", "\t階段を降りる" },
    { "\tgo up a staircase", "\t階段を登る" },
    { "\trest for a turn", "\tその場で休む" },
    { "\t(space) rest for a while", "\t(スペース) その場で休む" },
    { "\tpick something up", "\t足元の物を拾う" },
    { "\tinventory", "\t持ちものを見る" },
    { "\tinventory single item", "\t1つの持ちものを詳しく見る" },
    { "\tquaff potion", "\t水薬を飲む" },
    { "\tread scroll", "\t巻き物を読む" },
    { "\tread paper", "\t巻き物を読む" },
    { "\teat food", "\t食べ物を食べる" },
    { "\twield a weapon", "\t武器を手に持つ" },
    { "\twear armor", "\tよろいを着る" },
    { "\ttake armor off", "\tよろいを脱ぐ" },
    { "\tput on ring", "\t指輪をはめる" },
    { "\tremove ring", "\t指輪をはずす" },
    { "\tdrop object", "\t持ちものを落とす" },
    { "\tcall object", "\t持ちものに呼び名を付ける" },
    { "\trepeat last command", "\t直前のコマンドを繰り返す" },
    { "\tprint current weapon", "\t現在の武器を見る" },
    { "\tprint current armor", "\t現在のよろいを見る" },
    { "\tprint current rings", "\t現在の指輪を見る" },
    { "\tprint current stats", "\t現在の状態を表示" },
    { "\trecall what's been discovered", "\t発見済みの物を表示" },
    { "\texamine/set options", "\tオプションを表示・設定" },
    { "\tredraw screen", "\t画面を描き直す" },
    { "\trepeat last message", "\t最後のメッセージを再表示" },
    { "\tcancel command", "\tコマンドを中止" },
    { "\tsave game", "\tセーブして終了" },
    { "\tquit", "\tゲームを終了" },
    { "\tshell escape", "\tシェル呼び出し（HD2Dでは無効）" },
    { "\tprint version number", "\tバージョン情報を表示" },
    { "\tprint program version number", "\tバージョン情報を表示" },

    /* printf-style strings that go directly through the virtual curses API. */
    { "Level: %d  Gold: %-5d  Hp: %*d(%*d)  Str: %-2d", "階: %d  金塊: %-5d  体力: %*d(%*d)  強さ: %-2d" },
    { "  Ac: %-2d  Exp: %d/%d", "  守備: %-2d  経験: %d/%d" },
    { "Level: %d  Gold: %-5d  Hp: %*d(%*d)  Str: %2d(%d)  Arm: %-2d  Exp: %d/%d  %s", "階: %d  金塊: %-5d  体力: %*d(%*d)  強さ: %2d(%d)  守備: %-2d  経験: %d/%d  %s" },
    { "Level: %d  Gold: %-5d  Hp: %*d(%*d)  Str: %2d(%d)  Arm: %-2d  Exp: %d/%ld  %s", "階: %d  金塊: %-5d  体力: %*d(%*d)  強さ: %2d(%d)  守備: %-2d  経験: %d/%ld  %s" },
};

/* English game nouns that may appear inside dynamically composed text. */
static const rl_translation kNames[] = {
    /* 3.6 monsters. */
    { "giant ant", "巨大アリ" },
    { "floating eye", "浮遊する目" },
    { "violet fungi", "紫の菌類" },
    { "gnome", "ノーム" },
    { "invisible stalker", "透明な追跡者" },
    { "jackal", "ジャッカル" },
    { "kobold", "コボルド" },
    { "mimic", "ミミック" },
    { "purple worm", "紫ワーム" },
    { "quasit", "クアジット" },
    { "rust monster", "さびの怪物" },
    { "umber hulk", "アンバーハルク" },
    { "xorn", "ゾーン" },

    /* 5.4 / classic monsters (RogueClone2 Japanese terminology where available). */
    { "aquator", "水ごけの怪物" },
    { "bat", "大こうもり" },
    { "centaur", "ケンタウロス" },
    { "dragon", "ドラゴン" },
    { "emu", "大うずら" },
    { "venus flytrap", "はえとりぐさ" },
    { "griffin", "翼ライオン" },
    { "hobgoblin", "小鬼" },
    { "ice monster", "氷の怪物" },
    { "jabberwock", "巨大トカゲ" },
    { "kestrel", "大はやぶさ" },
    { "leprechaun", "金持ち妖精" },
    { "medusa", "メデューサ" },
    { "nymph", "ニンフ" },
    { "orc", "欲ばり鬼" },
    { "phantom", "幽霊" },
    { "quagga", "大つのじか" },
    { "rattlesnake", "がらがらへび" },
    { "snake", "へび" },
    { "troll", "巨人" },
    { "black unicorn", "一角獣" },
    { "vampire", "バンパイア" },
    { "wraith", "死霊" },
    { "xeroc", "物まねの怪物" },
    { "yeti", "雪男" },
    { "zombie", "ゾンビ" },

    /* Weapons. */
    { "two handed sword", "大きな剣" },
    { "crossbow bolt", "クロスボウの矢" },
    { "crossbow bolts", "クロスボウの矢" },
    { "long sword", "長い剣" },
    { "short bow", "弓" },
    { "int bow", "弓" },
    { "crossbow", "クロスボウ" },
    { "shuriken", "手裏剣" },
    { "shurikens", "手裏剣" },
    { "dagger", "短剣" },
    { "daggers", "短剣" },
    { "arrow", "矢" },
    { "arrows", "矢" },
    { "dart", "投げ矢" },
    { "darts", "投げ矢" },
    { "mace", "ほこ" },
    { "spear", "槍" },
    { "sling", "石投げ" },
    { "rock", "石" },
    { "rocks", "石" },

    /* Armor. */
    { "studded leather armor", "鋲付き革のよろい" },
    { "leather armor", "革のよろい" },
    { "ring mail", "かたびら" },
    { "scale mail", "うろこのよろい" },
    { "chain mail", "鎖かたびら" },
    { "banded mail", "帯金のよろい" },
    { "splint mail", "延金のよろい" },
    { "plate mail", "鋼鉄のよろい" },

    /* Magic effects. */
    { "identify ring, wand or staff", "指輪・つえの識別" },
    { "monster confusion", "怪物を混乱させる" },
    { "monster detection", "怪物を見つける" },
    { "magic detection", "魔法の品を見つける" },
    { "gold detection", "金塊を見つける" },
    { "food detection", "食糧を見つける" },
    { "magic mapping", "魔法の地図" },
    { "hold monster", "怪物を封じこめる" },
    { "enchant weapon", "武器に魔法をかける" },
    { "enchant armor", "よろいに魔法をかける" },
    { "identify potion", "水薬の識別" },
    { "identify scroll", "巻き物の識別" },
    { "identify weapon", "武器の識別" },
    { "identify armor", "よろいの識別" },
    { "identify", "持ちものの識別" },
    { "scare monster", "怪物を近寄せない" },
    { "teleportation", "テレポート" },
    { "create monster", "怪物を作りだす" },
    { "remove curse", "のろいを解く" },
    { "aggravate monsters", "怪物を怒らせる" },
    { "aggravate monster", "怪物を怒らせる" },
    { "protect armor", "よろいを守る" },
    { "blank paper", "白紙" },
    { "genocide", "怪物を消し去る" },
    { "gain strength", "強さが増す" },
    { "restore strength", "強さが元にもどる" },
    { "extra healing", "体力がとても回復する" },
    { "healing", "体力が回復する" },
    { "poison", "毒" },
    { "hallucination", "幻覚をおこす" },
    { "confusion", "頭が混乱する" },
    { "paralysis", "体が動かなくなる" },
    { "see invisible", "見えないものが見える" },
    { "raise level", "経験が増す" },
    { "haste self", "素早くなる" },
    { "blindness", "目が見えなくなる" },
    { "levitation", "空中に浮きあがる" },
    { "thirst quenching", "のどを潤す" },
    { "protection", "防御" },
    { "add strength", "強さが増す" },
    { "sustain strength", "強さが減らない" },
    { "searching", "隠れたものを見つける" },
    { "adornment", "飾りもの" },
    { "dexterity", "機敏になる" },
    { "increase damage", "攻撃力が増す" },
    { "regeneration", "体力が復活する" },
    { "slow digestion", "食べものが長持ちする" },
    { "stealth", "身を隠す" },
    { "maintain armor", "よろいが丈夫になる" },
    { "striking", "打撃" },
    { "lightning", "いなずま" },
    { "fire", "ほのお" },
    { "cold", "冷気" },
    { "polymorph", "ほかの怪物にする" },
    { "magic missile", "魔法のミサイル" },
    { "haste monster", "怪物を素早くする" },
    { "slow monster", "怪物を遅くする" },
    { "drain life", "生命力を吸う" },
    { "nothing", "役に立たない" },
    { "teleport away", "怪物を遠くに飛ばす" },
    { "teleport to", "怪物を引き寄せる" },
    { "cancellation", "魔力を封じる" },
    { "invisibility", "怪物を見えなくする" },
    { "light", "明かり" },
    { "sleep", "眠り" },

    /* Colors / materials commonly shown for unidentified objects. */
    { "aquamarine", "水色" }, { "burgundy", "ぶどう色" },
    { "vermilion", "朱色" }, { "tangerine", "だいだい色" },
    { "crimson", "深紅" }, { "magenta", "赤紫" },
    { "purple", "紫" }, { "yellow", "黄色" }, { "orange", "オレンジ色" },
    { "green", "緑" }, { "black", "黒" }, { "white", "白" },
    { "brown", "茶色" }, { "silver", "銀色" }, { "clear", "透明" },
    { "plaid", "格子柄" }, { "beige", "ベージュ" }, { "grey", "灰色" },
    { "gray", "灰色" }, { "blue", "青" }, { "red", "赤" },
    { "pink", "ピンク" }, { "violet", "すみれ色" }, { "amber", "琥珀色" },
    { "cyan", "青緑" }, { "ecru", "生成り" }, { "tan", "黄褐色" },
    { "topaz", "トパーズ" }, { "turquoise", "トルコ石" },
    { "diamond", "ダイヤ" }, { "emerald", "エメラルド" },
    { "amethyst", "紫水晶" }, { "ruby", "ルビー" }, { "sapphire", "サファイア" },
    { "saphire", "サファイア" }, { "opal", "オパール" }, { "pearl", "真珠" },
    { "agate", "めのう" }, { "garnet", "ざくろ石" }, { "jade", "ひすい" },
    { "lapis lazuli", "るり" }, { "lapus lazuli", "るり" },
    { "tiger eye", "虎目石" }, { "onyx", "オニキス" }, { "obsidian", "黒曜石" },
    { "moonstone", "月長石" }, { "carnelian", "紅玉髄" },
    { "alexandrite", "アレキサンドライト" }, { "granite", "花こう岩" },
    { "steel", "鋼鉄" }, { "bronze", "青銅" }, { "gold", "金" },
    { "silver", "銀" }, { "copper", "銅" }, { "nickel", "ニッケル" },
    { "cobalt", "コバルト" }, { "tin", "すず" }, { "iron", "鉄" },
    { "magnesium", "マグネシウム" }, { "chrome", "クロム" },
    { "platinum", "プラチナ" }, { "silicon", "シリコン" }, { "titanium", "チタン" },
    { "teak", "チーク" }, { "oak", "樫" }, { "oaken", "樫" },
    { "cherry", "桜" }, { "birch", "白樺" }, { "pine", "松" },
    { "cedar", "杉" }, { "redwood", "アメリカ杉" }, { "balsa", "バルサ" },
    { "walnut", "胡桃" }, { "maple", "楓" }, { "mahogany", "マホガニー" },
    { "elm", "楡" }, { "persimmon wood", "柿" },

    /* Trap names. */
    { "a mysterious trap", "正体不明のわな" },
    { "a poison dart trap", "毒矢のわな" },
    { "a sleeping gas trap", "催眠ガスのわな" },
    { "an arrow trap", "矢のわな" },
    { "a teleport trap", "テレポートのわな" },
    { "a beartrap", "熊のわな" },
    { "a trapdoor", "落し穴のわな" },
    { "a rust trap", "さびのわな" },

    /* 持ち物の名前の中の固有の品（inv_name が "The Amulet of Yendor" と出す）。 */
    { "Amulet of Yendor", "イェンダーの魔除け" },
};

/*
 * 未鑑定の持ち物の名前に付く材質（杖・つえの木と金属、指輪の石）。原作 init.c の wood[]・metal[]・stones[]
 * （3.6 と 5.4）のうち、上の kNames に無かった語。lead・bone・fall・holly のように文の中にもありうる語が
 * 混じるので、kNames には入れず、持ち物の行と判じたときの置き換えだけがこの表を使う。
 * 追加するときは tools/rogue_legacy/i18n_items_check.py（原作の配列を全数当てる）で漏れを確かめる。
 */
static const rl_translation kMaterials[] = {
    /* 木（3.6 と 5.4 の wood[] のうち未登録だった語）。 */
    { "avocado wood", "アボカド" }, { "banyan", "バニヤン" }, { "cinnibar", "シナバー" },
    { "driftwood", "流木" }, { "ebony", "黒檀" }, { "eucalyptus", "ユーカリ" },
    { "hemlock", "ツガ" }, { "ironwood", "鉄木" }, { "manzanita", "マンザニータ" },
    { "rosewood", "紫檀" }, { "zebra wood", "ゼブラウッド" }, { "zebrawood", "ゼブラウッド" },
    { "bamboo", "竹" }, { "cypress", "糸杉" }, { "dogwood", "ハナミズキ" },
    { "fall", "フォール" }, { "holly", "柊" }, { "kukui wood", "ククイ" },
    { "pecan", "ペカン" }, { "poplar", "ポプラ" }, { "spruce", "トウヒ" },
    /* 金属（metal[]）。 */
    { "aluminium", "アルミニウム" }, { "aluminum", "アルミニウム" }, { "bone", "骨" },
    { "brass", "真鍮" }, { "lead", "鉛" }, { "pewter", "ピューター" }, { "zinc", "亜鉛" },
    { "beryllium", "ベリリウム" }, { "electrum", "エレクトラム" }, { "mercury", "水銀" },
    { "tungsten", "タングステン" },
    /* 石（stones[]）。 */
    { "kryptonite", "クリプトナイト" }, { "germanium", "ゲルマニウム" }, { "peridot", "ペリドット" },
    { "stibotantalite", "スティビオタンタル石" }, { "taaffeite", "ターフェアイト" }, { "zircon", "ジルコン" },
};

static char g_dynamic[8192];

static int ascii_equal_ci(const char *a, const char *b)
{
    if (a == NULL || b == NULL) return a == b;
    while (*a != 0 && *b != 0) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static const char *lookup_table(const rl_translation *table, size_t count, const char *text)
{
    size_t i;
    if (text == NULL) return "";
    for (i = 0; i < count; ++i) {
        if (strcmp(text, table[i].en) == 0) return table[i].ja;
    }
    for (i = 0; i < count; ++i) {
        if (ascii_equal_ci(text, table[i].en)) return table[i].ja;
    }
    return NULL;
}

static int ascii_ieq_n(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (tolower(ca) != tolower(cb)) return 0;
    }
    return 1;
}

static int starts_with_ci(const char *text, const char *prefix)
{
    size_t n = strlen(prefix);
    return strlen(text) >= n && ascii_ieq_n(text, prefix, n);
}

static int ends_with(const char *text, const char *suffix)
{
    size_t nt = strlen(text);
    size_t ns = strlen(suffix);
    return nt >= ns && strcmp(text + nt - ns, suffix) == 0;
}

static const char *name_exact(const char *name)
{
    const char *translated;
    char temp[256];
    size_t n;
    const char *p = name;

    if (name == NULL) return "";
    while (*p == ' ') ++p;
    if (starts_with_ci(p, "the ")) p += 4;
    translated = lookup_table(kNames, sizeof(kNames) / sizeof(kNames[0]), p);
    if (translated != NULL) return translated;

    n = strlen(p);
    while (n > 0 && (p[n - 1] == '.' || p[n - 1] == '!' || p[n - 1] == '?' || p[n - 1] == ' ')) --n;
    if (n > 0 && n < sizeof(temp)) {
        memcpy(temp, p, n);
        temp[n] = 0;
        translated = lookup_table(kNames, sizeof(kNames) / sizeof(kNames[0]), temp);
        if (translated != NULL) return translated;
    }
    if (strcmp(p, "you") == 0 || strcmp(p, "You") == 0) return "あなた";
    if (strcmp(p, "it") == 0 || strcmp(p, "It") == 0) return "何者か";
    return NULL;
}

static int append_piece(char *out, size_t out_size, size_t *used, const char *piece)
{
    size_t n;
    if (piece == NULL) return 1;
    n = strlen(piece);
    if (*used + n + 1 > out_size) return 0;
    memcpy(out + *used, piece, n);
    *used += n;
    out[*used] = 0;
    return 1;
}

static int is_boundary(unsigned char c)
{
    return c == 0 || !(isalnum(c) || c == '_' || c == '-');
}

/*
 * Translate only game nouns, never arbitrary prose.
 * with_materials は持ち物の行（translate_item_line）だけが真にする。材質の表 kMaterials は
 * lead・bone・fall のように文の中にもありうる語を含むので、文の主語の訳（translate_subject）には使わない。
 */
static int replace_names(const char *text, char *out, size_t out_size, int with_materials)
{
    size_t used = 0;
    size_t pos = 0;
    size_t text_len = strlen(text);
    int changed = 0;
    while (pos < text_len) {
        size_t i;
        const rl_translation *best = NULL;
        size_t best_len = 0;
        const unsigned char before = pos == 0 ? 0 : (unsigned char)text[pos - 1];
        for (i = 0; i < sizeof(kNames) / sizeof(kNames[0]) + (with_materials ? sizeof(kMaterials) / sizeof(kMaterials[0]) : 0); ++i) {
            const rl_translation *entry = i < sizeof(kNames) / sizeof(kNames[0])
                ? &kNames[i] : &kMaterials[i - sizeof(kNames) / sizeof(kNames[0])];
            size_t n = strlen(entry->en);
            unsigned char after;
            if (n <= best_len || pos + n > text_len) continue;
            if (!ascii_ieq_n(text + pos, entry->en, n)) continue;
            after = pos + n >= text_len ? 0 : (unsigned char)text[pos + n];
            if (!is_boundary(before) || !is_boundary(after)) continue;
            best = entry;
            best_len = n;
        }
        if (best != NULL) {
            if (!append_piece(out, out_size, &used, best->ja)) return 0;
            pos += best_len;
            changed = 1;
        } else {
            char one[2];
            one[0] = text[pos++];
            one[1] = 0;
            if (!append_piece(out, out_size, &used, one)) return 0;
        }
    }
    return changed;
}

static const char *translate_subject(const char *text, char *scratch, size_t scratch_size)
{
    const char *exact = name_exact(text);
    if (exact != NULL) return exact;
    if (replace_names(text, scratch, scratch_size, 0)) return scratch;
    return text;
}

static const char *battle_relation(const char *text, const char *needle, const char *verb)
{
    const char *mid = strstr(text, needle);
    char left[512], right[512], lja[512], rja[512];
    size_t left_len;
    size_t right_len;
    const char *ls;
    const char *rs;
    if (mid == NULL) return NULL;
    left_len = (size_t)(mid - text);
    right_len = strlen(mid + strlen(needle));
    while (right_len > 0 && strchr(".! ", mid[strlen(needle) + right_len - 1]) != NULL) --right_len;
    if (left_len == 0 || left_len >= sizeof(left) || right_len == 0 || right_len >= sizeof(right)) return NULL;
    memcpy(left, text, left_len); left[left_len] = 0;
    memcpy(right, mid + strlen(needle), right_len); right[right_len] = 0;
    ls = translate_subject(left, lja, sizeof(lja));
    rs = translate_subject(right, rja, sizeof(rja));
    snprintf(g_dynamic, sizeof(g_dynamic), "%sは%sに%s。", ls, rs, verb);
    return g_dynamic;
}

static int looks_like_item(const char *text)
{
    static const char *keys[] = {
        "potion", "scroll", "ring", "wand", "staff", "armor", "mail", "sword", "bow", "arrow",
        "dagger", "mace", "spear", "shuriken", "dart", "sling", "rock", "food", "slime-mold", "Amulet of Yendor",
        /* HD2D: 好きな果物の既定の日本語名（項目 2）。英字の "slime-mold" を訳す
           経路とは別に、この語もアイテム行と見なさないと拾う・食べるの行が
           英日混在のまま残る（rogue_restore 第 2 回・項目 3）。 */
        "\xe3\x82\xb9\xe3\x83\xa9\xe3\x82\xa4\xe3\x83\xa0\xe3\x83\xa2\xe3\x83\xbc\xe3\x83\xab\xe3\x83\x89"
    };
    size_t i;
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) if (strstr(text, keys[i]) != NULL) return 1;
    return 0;
}

/*
 * HD2D: 原作は複数形を "%s" + "s" で作る（things.c の "%d %ss"）。英単語なら
 * そのまま複数形になるが、日本語の果物名（既定の「スライムモールド」や、
 * 遊ぶ人が設定で入れた別の日本語名）に "s" が付くと「スライムモールドs」の
 * ように英日混在で壊れる。直前が UTF-8 の続きバイト（0x80 以上）で、直後が
 * 英字ではない（語の続きではない）'s' は、日本語の複数形ではなく原作の
 * 貼り付けなので取り除く（rogue_restore 第 2 回・項目 3。遊ぶ人が選んだ
 * 果物名にも効くよう、特定の語ではなくバイト種別で判定する）。
 */
static void strip_glued_plural_s(char *text)
{
    size_t len = strlen(text);
    size_t i;
    for (i = 0; i < len; ++i) {
        if (text[i] != 's') continue;
        if (i == 0 || (unsigned char)text[i - 1] < 0x80) continue;
        if (isalpha((unsigned char)text[i + 1])) continue;
        memmove(text + i, text + i + 1, len - i); /* includes the trailing NUL */
        --len;
        --i;
    }
}

static void remove_leading_article(char *text)
{
    char *p = text;
    if (isalpha((unsigned char)p[0]) && p[1] == ')' && p[2] == ' ') p += 3;
    /* 5.4 は小文字で書く（"you now have a scroll ..."）。大文字だけ見ていたので「a 巻き物」が残った。 */
    if (strncmp(p, "A ", 2) == 0 || strncmp(p, "a ", 2) == 0) memmove(p, p + 2, strlen(p + 2) + 1);
    else if (strncmp(p, "An ", 3) == 0 || strncmp(p, "an ", 3) == 0) memmove(p, p + 3, strlen(p + 3) + 1);
    else if (strncmp(p, "Some ", 5) == 0 || strncmp(p, "some ", 5) == 0) memmove(p, p + 5, strlen(p + 5) + 1);
}

/*
 * 巻き物の題名（原作が英字の音節を無作為に並べた 'muryumi lechod'）を、ローマ字として
 * ひらがなへ置き換える。RogueClone2 の日本語版はひらがなの音節で題名を作るので、それに
 * そろえる。題名そのものはセーブに英字で残るので、表示のときだけ置き換える。
 * 同じ題名は必ず同じかなになる（見分けが付く）。読みの正しさは求めない。
 */
static const char *kana_row(char consonant, int vowel)
{
    static const char *const rows[][6] = {
        /* 子音, a, i, u, e, o */
        { "",  "あ", "い", "う", "え", "お" },
        { "k", "か", "き", "く", "け", "こ" },
        { "g", "が", "ぎ", "ぐ", "げ", "ご" },
        { "s", "さ", "し", "す", "せ", "そ" },
        { "z", "ざ", "じ", "ず", "ぜ", "ぞ" },
        { "t", "た", "ち", "つ", "て", "と" },
        { "d", "だ", "ぢ", "づ", "で", "ど" },
        { "n", "な", "に", "ぬ", "ね", "の" },
        { "h", "は", "ひ", "ふ", "へ", "ほ" },
        { "b", "ば", "び", "ぶ", "べ", "ぼ" },
        { "p", "ぱ", "ぴ", "ぷ", "ぺ", "ぽ" },
        { "m", "ま", "み", "む", "め", "も" },
        { "y", "や", "い", "ゆ", "いぇ", "よ" },
        { "r", "ら", "り", "る", "れ", "ろ" },
        { "w", "わ", "うぃ", "う", "うぇ", "を" },
        { "f", "ふぁ", "ふぃ", "ふ", "ふぇ", "ふぉ" },
        { "v", "ゔぁ", "ゔぃ", "ゔ", "ゔぇ", "ゔぉ" },
        { "j", "じゃ", "じ", "じゅ", "じぇ", "じょ" },
    };
    size_t i;
    /* l→r、c・q→k、x→ks は近い音へ寄せる（x は呼び出し側で k として扱う）。 */
    if (consonant == 'l') consonant = 'r';
    if (consonant == 'c' || consonant == 'q' || consonant == 'x') consonant = 'k';
    for (i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        if (rows[i][0][0] == consonant) return rows[i][vowel + 1];
    }
    return rows[0][vowel + 1];
}

static int vowel_index(char c)
{
    switch (c) {
    case 'a': return 0;
    case 'i': return 1;
    case 'u': return 2;
    case 'e': return 3;
    case 'o': return 4;
    default: return -1;
    }
}

static void romaji_to_hiragana(const char *in, size_t len, char *out, size_t size)
{
    size_t used = 0;
    size_t i = 0;
    out[0] = 0;
#define RL_PUT(s) do { const char *s_ = (s); size_t n_ = strlen(s_); \
        if (used + n_ + 1 < size) { memcpy(out + used, s_, n_); used += n_; out[used] = 0; } } while (0)
    while (i < len) {
        const char c = (char)tolower((unsigned char)in[i]);
        const char c1 = i + 1 < len ? (char)tolower((unsigned char)in[i + 1]) : 0;
        const char c2 = i + 2 < len ? (char)tolower((unsigned char)in[i + 2]) : 0;
        int v = vowel_index(c);
        if (!isalpha((unsigned char)c)) { char one[2] = { c, 0 }; RL_PUT(c == ' ' ? " " : one); ++i; continue; }
        if (v >= 0) { RL_PUT(kana_row(0, v)); ++i; continue; }
        /* 同じ子音が 2 つ続けば促音（n を除く）。 */
        if (c == c1 && c != 'n') { RL_PUT("っ"); ++i; continue; }
        if (c == 'n' && vowel_index(c1) < 0 && c1 != 'y') { RL_PUT("ん"); ++i; continue; }
        if (c == 's' && c1 == 'h' && vowel_index(c2) >= 0) {
            static const char *const sh[] = { "しゃ", "し", "しゅ", "しぇ", "しょ" };
            RL_PUT(sh[vowel_index(c2)]); i += 3; continue;
        }
        if (c == 'c' && c1 == 'h' && vowel_index(c2) >= 0) {
            static const char *const ch[] = { "ちゃ", "ち", "ちゅ", "ちぇ", "ちょ" };
            RL_PUT(ch[vowel_index(c2)]); i += 3; continue;
        }
        if (c == 't' && c1 == 's' && c2 == 'u') { RL_PUT("つ"); i += 3; continue; }
        if (c1 == 'y' && vowel_index(c2) >= 0 && c != 'y') {
            /* 拗音（kya → きゃ）。い段の字に小さい や・ゆ・よ を付ける（い・え段は そのまま）。 */
            static const char *const small[] = { "ゃ", "ぃ", "ゅ", "ぇ", "ょ" };
            RL_PUT(kana_row(c, 1)); RL_PUT(small[vowel_index(c2)]); i += 3; continue;
        }
        if (vowel_index(c1) >= 0) {
            if (c == 'x') RL_PUT("く");
            RL_PUT(kana_row(c, vowel_index(c1))); i += 2; continue;
        }
        /* 母音の付かない子音。t・d は お段、ほかは う段に寄せる（外来語の読み方）。 */
        RL_PUT(kana_row(c, (c == 't' || c == 'd') ? 4 : 2));
        ++i;
    }
#undef RL_PUT
}

/* "a scroll titled 'muryumi lechod'" を「「むりゅみ れちょど」という巻き物」へ組み替える。 */
static void convert_scroll_title(char *text, size_t size)
{
    char *titled = strstr(text, " titled '");
    char *start;
    char *end;
    char kana[512];
    char result[8192];
    int count = 0;
    const char *p;
    if (titled == NULL) return;
    start = titled + strlen(" titled '");
    end = strchr(start, '\'');
    if (end == NULL) return;
    /* 題名の前は "a scroll" / "2 scrolls" / "scroll"。数だけ拾い、ほかは組み替えで捨てる。 */
    p = text;
    if (isalpha((unsigned char)p[0]) && p[1] == ')' && p[2] == ' ') p += 3;
    if (isdigit((unsigned char)*p)) count = atoi(p);
    romaji_to_hiragana(start, (size_t)(end - start), kana, sizeof(kana));
    {
        char prefix[64] = "";
        if (isalpha((unsigned char)text[0]) && text[1] == ')' && text[2] == ' ') snprintf(prefix, sizeof(prefix), "%.3s", text);
        if (count > 1) snprintf(result, sizeof(result), "%s%d個の「%s」という巻き物%s", prefix, count, kana, end + 1);
        else snprintf(result, sizeof(result), "%s「%s」という巻き物%s", prefix, kana, end + 1);
    }
    strncpy(text, result, size - 1);
    text[size - 1] = 0;
}

static void replace_ascii_phrase(char *buffer, size_t size, const char *from, const char *to)
{
    char temp[8192];
    char *at;
    size_t prefix;
    size_t need;
    at = strstr(buffer, from);
    if (at == NULL) return;
    prefix = (size_t)(at - buffer);
    need = prefix + strlen(to) + strlen(at + strlen(from)) + 1;
    if (need > sizeof(temp) || need > size) return;
    memcpy(temp, buffer, prefix);
    temp[prefix] = 0;
    strcat(temp, to);
    strcat(temp, at + strlen(from));
    strcpy(buffer, temp);
}

/*
 * 持ち物の名前（inv_name が組む行）の形かどうか。原作は "A scroll of magic mapping."・"2 blue potions"・
 * "d) a staff of light [10 charges](cherry)" のように、先頭から「冠詞・数・形容の語を数語＋種類の語」を並べ、
 * そのあとに of・called・titled・括弧・文末の . ! が来る。文（"oh, now this scroll has a map on it"）は
 * 句読点や助動詞などの語を含み、形が違う。種別語（potion・scroll など）の置き換えは、この形のときだけにする
 * （文は表で完全に引くのを先にする。表に無い文を部分的に置き換えると "Oh, now this 巻き物 has a map on it"
 * のように英日混在になる）。
 */
static int is_item_phrase_stopword(const char *token, size_t n)
{
    static const char *const words[] = {
        "you", "your", "i", "he", "she", "it", "its", "they", "we", "is", "are", "was", "were", "be", "been",
        "has", "have", "had", "do", "does", "did", "this", "that", "these", "those", "there", "here", "to", "in",
        "on", "at", "as", "and", "or", "but", "with", "from", "by", "for", "not", "no", "now", "oh", "can",
        "can't", "will", "would", "could", "should", "may", "must", "if", "then", "when", "what", "which",
        "who", "how", "hear", "see", "feel", "feels", "find", "found", "turns", "turn", "glows", "glow",
        "hits", "hit", "misses", "miss", "vanishes", "falls", "into", "out", "up", "down", "over",
    };
    size_t i;
    /* fall は 5.4 の木の名前（"A fall wand"）なので、ここには入れない（falls は動詞だけ）。 */
    for (i = 0; i < sizeof(words) / sizeof(words[0]); ++i) {
        if (strlen(words[i]) == n && ascii_ieq_n(token, words[i], n)) return 1;
    }
    return 0;
}

static int is_item_phrase(const char *text)
{
    const char *p = text;
    int tokens = 0;
    if (isalpha((unsigned char)p[0]) && p[1] == ')' && p[2] == ' ') p += 3;
    for (;;) {
        const char *start;
        size_t n;
        int ended = 0;
        while (*p == ' ') ++p;
        if (*p == '\0' || *p == '(' || *p == '[') break;
        start = p;
        while (*p != '\0' && *p != ' ' && *p != '(' && *p != '[') {
            const unsigned char c = (unsigned char)*p;
            if (c == ',') {
                /* "+1,+2" のような命中と威力の組だけは 1 語。それ以外の読点は文の印。 */
                if (!(p > start && isdigit((unsigned char)p[-1])
                      && (p[1] == '+' || p[1] == '-' || isdigit((unsigned char)p[1])))) return 0;
            } else if (strchr(";:?\"%*=<>", c) != NULL) {
                return 0;
            }
            ++p;
        }
        n = (size_t)(p - start);
        while (n > 0 && (start[n - 1] == '.' || start[n - 1] == '!')) { --n; ended = 1; }
        if (n == 0) {
            if (tokens == 0) return 0;
            break;
        }
        if (tokens > 0 && ((n == 2 && ascii_ieq_n(start, "of", 2)) || (n == 6 && ascii_ieq_n(start, "called", 6))
                           || (n == 6 && ascii_ieq_n(start, "titled", 6)))) break;
        /* 冠詞（the・a・an・some）と数と形容の語は通す。代名詞・助動詞・前置詞などは文の語。 */
        if (is_item_phrase_stopword(start, n)) return 0;
        ++tokens;
        if (tokens > 6) return 0;
        if (ended) {
            while (*p == ' ') ++p;
            if (*p != '\0' && *p != '(' && *p != '[') return 0;
            break;
        }
    }
    return tokens >= 1;
}

/* 種類の語の直後が、名詞句の終わり方（文末・of・called・titled・括弧）か。 */
static int kind_follower_ok(const char *rest)
{
    if (*rest == '\0' || *rest == '(' || *rest == '[') return 1;
    if (*rest == '.' || *rest == '!') {
        while (*rest == '.' || *rest == '!' || *rest == ' ') ++rest;
        return *rest == '\0';
    }
    return strncmp(rest, " of ", 4) == 0 || strncmp(rest, " called ", 8) == 0
        || strncmp(rest, " titled ", 8) == 0 || strncmp(rest, " (", 2) == 0 || strncmp(rest, " [", 2) == 0;
}

/*
 * 種類の語（potion・scroll・ring・wand・staff）を、語の境界にあって名詞句の末尾に当たる最初の 1 か所だけ置き換える。
 * 以前は直前に空白を要求していたので、冠詞を取った行頭（"scroll of …"）に当たらなかった。
 */
static void replace_kind_word(char *buffer, size_t size, const char *word, const char *ja)
{
    const size_t n = strlen(word);
    char *at = buffer;
    while ((at = strstr(at, word)) != NULL) {
        const unsigned char before = at == buffer ? 0 : (unsigned char)at[-1];
        if (is_boundary(before) && is_boundary((unsigned char)at[n]) && kind_follower_ok(at + n)) {
            char temp[8192];
            const size_t prefix = (size_t)(at - buffer);
            const size_t need = prefix + strlen(ja) + strlen(at + n) + 1;
            if (need > sizeof(temp) || need > size) return;
            memcpy(temp, buffer, prefix);
            temp[prefix] = 0;
            strcat(temp, ja);
            strcat(temp, at + n);
            strcpy(buffer, temp);
            return;
        }
        at += n;
    }
}

static const char *translate_item_line(const char *text)
{
    char replaced[8192];
    if (!looks_like_item(text)) return NULL;
    if (!is_item_phrase(text)) return NULL;
    if (!replace_names(text, replaced, sizeof(replaced), 1)) {
        strncpy(replaced, text, sizeof(replaced) - 1);
        replaced[sizeof(replaced) - 1] = 0;
    }
    convert_scroll_title(replaced, sizeof(replaced));
    remove_leading_article(replaced);
    replace_ascii_phrase(replaced, sizeof(replaced), "The イェンダーの魔除け", "イェンダーの魔除け");
    replace_ascii_phrase(replaced, sizeof(replaced), "the イェンダーの魔除け", "イェンダーの魔除け");
    replace_kind_word(replaced, sizeof(replaced), "potions", "水薬");
    replace_kind_word(replaced, sizeof(replaced), "potion", "水薬");
    replace_kind_word(replaced, sizeof(replaced), "scrolls", "巻き物");
    replace_kind_word(replaced, sizeof(replaced), "scroll", "巻き物");
    replace_kind_word(replaced, sizeof(replaced), "rings", "指輪");
    replace_kind_word(replaced, sizeof(replaced), "ring", "指輪");
    replace_kind_word(replaced, sizeof(replaced), "wands", "つえ");
    replace_kind_word(replaced, sizeof(replaced), "wand", "つえ");
    replace_kind_word(replaced, sizeof(replaced), "staffs", "つえ");
    replace_kind_word(replaced, sizeof(replaced), "staff", "つえ");
    replace_ascii_phrase(replaced, sizeof(replaced), "rations of food", "食糧");
    replace_ascii_phrase(replaced, sizeof(replaced), "food", "食糧");
    replace_ascii_phrase(replaced, sizeof(replaced), " of ", "・");
    replace_ascii_phrase(replaced, sizeof(replaced), " called ", "（呼称: ");
    replace_ascii_phrase(replaced, sizeof(replaced), " titled ", "（題名: ");
    replace_ascii_phrase(replaced, sizeof(replaced), " (being worn)", "（着用中）");
    replace_ascii_phrase(replaced, sizeof(replaced), " (weapon in hand)", "（使用中）");
    replace_ascii_phrase(replaced, sizeof(replaced), " (on left hand)", "（左手）");
    replace_ascii_phrase(replaced, sizeof(replaced), " (on right hand)", "（右手）");
    replace_ascii_phrase(replaced, sizeof(replaced), "protection ", "守備 ");
    /* 識別済みの杖の使用回数（sticks.c の charge_str が " [%d charges]" と組む）。 */
    replace_ascii_phrase(replaced, sizeof(replaced), " charges]", "回分]");
    strip_glued_plural_s(replaced);
    strncpy(g_dynamic, replaced, sizeof(g_dynamic) - 1);
    g_dynamic[sizeof(g_dynamic) - 1] = 0;
    return g_dynamic;
}

static const char *translate_dynamic(const char *text)
{
    const char *p;
    const char *rel;
    const char *item;
    char subject[1024];
    char translated[1024];

    if (text == NULL || *text == 0) return NULL;

    /* Rogue 5.4 composes option labels as '<prompt> ("name"): ' before the
       string reaches curses.  Translate only the known prompt prefix and keep
       the original option key visible for historical fidelity. */
    {
        const char *option_marker = strstr(text, " (\"");
        if (option_marker != NULL) {
            char prefix[256];
            size_t n = (size_t)(option_marker - text);
            const char *translated_prefix = NULL;
            if (n > 0 && n < sizeof(prefix)) {
                memcpy(prefix, text, n);
                prefix[n] = 0;
                translated_prefix = lookup_table(kTranslations,
                    sizeof(kTranslations) / sizeof(kTranslations[0]), prefix);
            }
            if (translated_prefix != NULL) {
                snprintf(g_dynamic, sizeof(g_dynamic), "%s%s", translated_prefix, option_marker);
                return g_dynamic;
            }
        }
    }

    /* ヘルプの 1 行は「キー＋説明」で組まれる（"h\tleft"、"t<dir>\tthrow something"）。
       説明の側は表にあるので、先頭のキーの字を残して後ろだけ訳す。 */
    {
        const char *tab = strchr(text, '\t');
        if (tab != NULL && tab - text <= 8) {
            const char *start;
            for (start = text; start <= tab; ++start) {
                const char *rest = lookup_table(kTranslations,
                    sizeof(kTranslations) / sizeof(kTranslations[0]), start);
                if (rest != NULL && start > text) {
                    snprintf(g_dynamic, sizeof(g_dynamic), "%.*s%s", (int)(start - text), text, rest);
                    return g_dynamic;
                }
            }
        }
    }

    /* 知らないキーを押したときの "'u': unknown character"（3.6 は "'u' : ..."）。 */
    {
        const char *tail = strstr(text, "': unknown character");
        if (tail == NULL) tail = strstr(text, "' : unknown character");
        if (text[0] == '\'' && tail != NULL && tail > text + 1 && tail - text < 16) {
            snprintf(g_dynamic, sizeof(g_dynamic), "%.*s は使えないコマンドです。", (int)(tail - text + 1), text);
            return g_dynamic;
        }
    }

    /* "Please type one of !?=/ (ESCAPE to quit)" のように、選べる字を埋め込んだ案内。 */
    {
        static const char head[] = "Please type one of ";
        static const char tail[] = " (ESCAPE to quit)";
        const size_t length = strlen(text);
        if (length > sizeof(head) - 1 + sizeof(tail) - 1 && strncmp(text, head, sizeof(head) - 1) == 0
            && strcmp(text + length - (sizeof(tail) - 1), tail) == 0) {
            const int keys = (int)(length - (sizeof(head) - 1) - (sizeof(tail) - 1));
            snprintf(g_dynamic, sizeof(g_dynamic), "%.*s のどれかを押してください（エスケープでやめる）",
                keys, text + sizeof(head) - 1);
            return g_dynamic;
        }
    }

    /* get_item() builds this prompt in pieces, so the final sentence cannot be
       matched by a single source literal.  The exact purpose is already clear
       from the HD2D item-choice overlay; keep the prompt generic and accurate. */
    if (strstr(text, "(* for list):") != NULL
        && (starts_with_ci(text, "Which object do you want to ")
            || starts_with_ci(text, "which object do you want to ")
            || strstr(text, " what? (* for list):") != NULL)) {
        return "どの持ち物を選びますか？: ";
    }

    if (strstr(text, " is not a valid item") != NULL || strstr(text, " not in pack") != NULL
        || starts_with_ci(text, "Please specify a letter between ")
        || starts_with_ci(text, "Range is ")) {
        return "その持ち物は選べません。";
    }

    /* Rogue 3.6 assembles its entire status line in a legacy 80-byte buffer
       before it reaches curses, so translate the finished display string here
       instead of expanding UTF-8 inside that old game buffer. */
    if (starts_with_ci(text, "Level: ") && strstr(text, " Gold: ") != NULL
        && strstr(text, " Hp: ") != NULL && strstr(text, " Str: ") != NULL) {
        strncpy(g_dynamic, text, sizeof(g_dynamic) - 1);
        g_dynamic[sizeof(g_dynamic) - 1] = 0;
        replace_ascii_phrase(g_dynamic, sizeof(g_dynamic), "Level: ", "階: ");
        replace_ascii_phrase(g_dynamic, sizeof(g_dynamic), "Gold: ", "金塊: ");
        replace_ascii_phrase(g_dynamic, sizeof(g_dynamic), "Hp: ", "体力: ");
        replace_ascii_phrase(g_dynamic, sizeof(g_dynamic), "Str: ", "強さ: ");
        replace_ascii_phrase(g_dynamic, sizeof(g_dynamic), "Ac: ", "守備: ");
        replace_ascii_phrase(g_dynamic, sizeof(g_dynamic), "Exp: ", "経験: ");
        return g_dynamic;
    }

    if (starts_with_ci(text, "Welcome to level ")) {
        const char *level_text = text + strlen("Welcome to level ");
        /* 経験レベルが上がったときの文（check_level）。階ではない。RogueClone2 の公式訳に合わせる。 */
        snprintf(g_dynamic, sizeof(g_dynamic), "レベル%sへようこそ。", level_text);
        return g_dynamic;
    }
    if (starts_with_ci(text, "Rogue version ")) {
        p = text + strlen("Rogue version ");
        snprintf(g_dynamic, sizeof(g_dynamic), "Rogue バージョン %s", p);
        return g_dynamic;
    }
    if (starts_with_ci(text, "version ")) {
        p = text + strlen("version ");
        snprintf(g_dynamic, sizeof(g_dynamic), "Rogue バージョン %s", p);
        return g_dynamic;
    }
    if (starts_with_ci(text, "Illegal command '") || starts_with_ci(text, "Unknown character '")) {
        const char *mark = strchr(text, '\'');
        snprintf(g_dynamic, sizeof(g_dynamic), "その入力は使えない: %s", mark != NULL ? mark : text);
        return g_dynamic;
    }
    if (starts_with_ci(text, "Save file (")) {
        snprintf(g_dynamic, sizeof(g_dynamic), "セーブファイル%s", text + strlen("Save file"));
        return g_dynamic;
    }
    if (starts_with_ci(text, "File name: ")) {
        snprintf(g_dynamic, sizeof(g_dynamic), "ファイル名: %s", text + strlen("File name: "));
        return g_dynamic;
    }
    if (starts_with_ci(text, "food left: ")) {
        snprintf(g_dynamic, sizeof(g_dynamic), "残りの食糧: %s", text + strlen("food left: "));
        return g_dynamic;
    }
    if (strstr(text, " things in your pack") != NULL) {
        snprintf(g_dynamic, sizeof(g_dynamic), "持ちものは%s個。", text);
        replace_ascii_phrase(g_dynamic, sizeof(g_dynamic), " things in your pack", "");
        return g_dynamic;
    }
    if (starts_with_ci(text, "Started a wandering ")) {
        p = text + strlen("Started a wandering ");
        p = translate_subject(p, translated, sizeof(translated));
        snprintf(g_dynamic, sizeof(g_dynamic), "%sが徘徊を始めた。", p);
        return g_dynamic;
    }
    if (starts_with_ci(text, "My, that was a yummy ")) {
        snprintf(g_dynamic, sizeof(g_dynamic), "%sはとてもおいしかった。", text + strlen("My, that was a yummy "));
        return g_dynamic;
    }
    if (starts_with_ci(text, "This potion tastes like ") && ends_with(text, " juice.")) {
        size_t n = strlen(text) - strlen("This potion tastes like ") - strlen(" juice.");
        if (n < sizeof(subject)) {
            memcpy(subject, text + strlen("This potion tastes like "), n); subject[n] = 0;
            snprintf(g_dynamic, sizeof(g_dynamic), "この水薬は%sジュースの味がする。", subject);
            return g_dynamic;
        }
    }
    if (starts_with_ci(text, "The light in here suddenly seems ")) {
        p = text + strlen("The light in here suddenly seems ");
        p = translate_subject(p, translated, sizeof(translated));
        snprintf(g_dynamic, sizeof(g_dynamic), "辺りの光が急に%sく見えた。", p);
        return g_dynamic;
    }
    if (starts_with_ci(text, "A ") && strstr(text, " light flashes in your eyes") != NULL) {
        const char *end = strstr(text, " light flashes in your eyes");
        size_t n = (size_t)(end - (text + 2));
        if (n < sizeof(subject)) {
            memcpy(subject, text + 2, n); subject[n] = 0;
            p = translate_subject(subject, translated, sizeof(translated));
            snprintf(g_dynamic, sizeof(g_dynamic), "%sの光が目の前で閃いた。", p);
            return g_dynamic;
        }
    }
    if (strstr(text, " sparks dance across your armor") != NULL) {
        const char *end = strstr(text, " sparks dance across your armor");
        size_t n = (size_t)(end - text);
        if (n < sizeof(subject)) {
            memcpy(subject, text, n); subject[n] = 0;
            p = translate_subject(subject, translated, sizeof(translated));
            snprintf(g_dynamic, sizeof(g_dynamic), "%sの火花がよろいの表面を走った。", p);
            return g_dynamic;
        }
    }
    if (starts_with_ci(text, "Your pack turns ") || starts_with_ci(text, "You pack turns ")) {
        p = strstr(text, "turns "); p = p != NULL ? p + strlen("turns ") : text;
        p = translate_subject(p, translated, sizeof(translated));
        snprintf(g_dynamic, sizeof(g_dynamic), "持ちものが%sに変わった！", p);
        return g_dynamic;
    }
    if (starts_with_ci(text, "Your hands stop glowing ")) {
        p = text + strlen("Your hands stop glowing ");
        p = translate_subject(p, translated, sizeof(translated));
        snprintf(g_dynamic, sizeof(g_dynamic), "手の%sの輝きが消えた。", p);
        return g_dynamic;
    }
    if (starts_with_ci(text, "You feel less ") && ends_with(text, " now")) {
        snprintf(g_dynamic, sizeof(g_dynamic), "状態が回復してきた。" );
        return g_dynamic;
    }

    /* Symbol identification: preserve the key, localize only its meaning. */
    if (text[0] == '\'' && text[2] == '\'' && (text[3] == ':' || (text[3] == ' ' && text[4] == ':'))) {
        const char *meaning = strchr(text + 3, ':');
        const char *ja;
        if (meaning != NULL) {
            while (*++meaning == ' ') {}
            ja = name_exact(meaning);
            if (ja == NULL && replace_names(meaning, translated, sizeof(translated), 0)) ja = translated;
            if (ja != NULL) {
                snprintf(g_dynamic, sizeof(g_dynamic), "'%c' : %s", text[1], ja);
                return g_dynamic;
            }
        }
    }

    /* 投げた物が地面に当たって消える文は、戦闘の " hit " と取り違えない（3.6 "Your dagger vanishes as it hits the ground."・
       "a dart vanishes as it hits the ground."、5.4 "the dagger vanishes as it hits the ground"）。 */
    {
        const char *tail = strstr(text, " vanishes as it hits the ground");
        if (tail != NULL && tail > text && (size_t)(tail - text) < sizeof(subject)) {
            memcpy(subject, text, (size_t)(tail - text));
            subject[tail - text] = 0;
            p = subject;
            if (starts_with_ci(p, "your ")) p += 5;
            else if (starts_with_ci(p, "the ")) p += 4;
            item = translate_item_line(p);
            if (item != NULL) {
                snprintf(translated, sizeof(translated), "%s", item); /* item は g_dynamic の中なので写してから使う */
                p = translated;
            } else {
                p = translate_subject(p, translated, sizeof(translated));
            }
            snprintf(g_dynamic, sizeof(g_dynamic), "%sは地面に当たって消えた。", p);
            return g_dynamic;
        }
    }

    /* Passive hit text must be handled before the generic " hit " relation. */
    if (starts_with_ci(text, "You are hit by the ")) {
        p = text + strlen("You are hit by the ");
        p = translate_subject(p, translated, sizeof(translated));
        snprintf(g_dynamic, sizeof(g_dynamic), "%sが命中した。", p);
        return g_dynamic;
    }

    /* Common battle sentences assembled by addmsg()/endmsg(). */
    rel = battle_relation(text, " scored an excellent hit on ", "強烈な一撃を当てた"); if (rel) return rel;
    rel = battle_relation(text, " swing and hit ", "攻撃を当てた"); if (rel) return rel;
    rel = battle_relation(text, " swings and hits ", "攻撃を当てた"); if (rel) return rel;
    rel = battle_relation(text, " have injured ", "傷を負わせた"); if (rel) return rel;
    rel = battle_relation(text, " has injured ", "傷を負わせた"); if (rel) return rel;
    /* 否定は " hit " より先に見る。後にすると "The jackal doesn't hit you" の
       "doesn't" を主語に含めたまま「攻撃を当てた」と逆の意味に訳していた（2026-09-23）。 */
    rel = battle_relation(text, " don't hit ", "攻撃を外した"); if (rel) return rel;
    rel = battle_relation(text, " doesn't hit ", "攻撃を外した"); if (rel) return rel;
    rel = battle_relation(text, " hits ", "攻撃を当てた"); if (rel) return rel;
    rel = battle_relation(text, " hit ", "攻撃を当てた"); if (rel) return rel;
    rel = battle_relation(text, " swing and miss ", "攻撃を外した"); if (rel) return rel;
    rel = battle_relation(text, " swings and misses ", "攻撃を外した"); if (rel) return rel;
    rel = battle_relation(text, " barely miss ", "わずかに攻撃を外した"); if (rel) return rel;
    rel = battle_relation(text, " barely misses ", "わずかに攻撃を外した"); if (rel) return rel;
    rel = battle_relation(text, " don't hit ", "攻撃を外した"); if (rel) return rel;
    rel = battle_relation(text, " doesn't hit ", "攻撃を外した"); if (rel) return rel;
    rel = battle_relation(text, " misses ", "攻撃を外した"); if (rel) return rel;
    rel = battle_relation(text, " miss ", "攻撃を外した"); if (rel) return rel;

    if (starts_with_ci(text, "Defeated the ")) {
        p = text + strlen("Defeated the ");
        p = translate_subject(p, translated, sizeof(translated));
        snprintf(g_dynamic, sizeof(g_dynamic), "%sを倒した。", p);
        return g_dynamic;
    }
    if (starts_with_ci(text, "You have defeated ")) {
        p = text + strlen("You have defeated ");
        if (starts_with_ci(p, "the ")) p += 4;
        p = translate_subject(p, translated, sizeof(translated));
        snprintf(g_dynamic, sizeof(g_dynamic), "%sを倒した。", p);
        return g_dynamic;
    }
    if (starts_with_ci(text, "You found ")) {
        p = text + strlen("You found ");
        if (strstr(p, "gold piece") != NULL) {
            long amount = 0;
            if (sscanf(p, "%ld gold", &amount) == 1) {
                snprintf(g_dynamic, sizeof(g_dynamic), "%ld個の金塊を見つけた。", amount);
            } else {
                snprintf(g_dynamic, sizeof(g_dynamic), "金塊を見つけた。" );
            }
            return g_dynamic;
        }
        p = translate_subject(p, translated, sizeof(translated));
        snprintf(g_dynamic, sizeof(g_dynamic), "%sを見つけた。", p);
        return g_dynamic;
    }
    if (starts_with_ci(text, "You have found ")) {
        p = text + strlen("You have found ");
        item = translate_item_line(p);
        snprintf(subject, sizeof(subject), "%s", item != NULL ? item : p);
        snprintf(g_dynamic, sizeof(g_dynamic), "%sを見つけた。", subject);
        return g_dynamic;
    }
    if (starts_with_ci(text, "You now have ")) {
        p = text + strlen("You now have ");
        item = translate_item_line(p);
        snprintf(subject, sizeof(subject), "%s", item != NULL ? item : p);
        snprintf(g_dynamic, sizeof(g_dynamic), "%sを手に入れた。", subject);
        return g_dynamic;
    }
    if (starts_with_ci(text, "She stole ")) {
        p = text + strlen("She stole ");
        item = translate_item_line(p);
        snprintf(subject, sizeof(subject), "%s", item != NULL ? item : p);
        if (ends_with(subject, "!")) subject[strlen(subject) - 1] = 0;
        snprintf(g_dynamic, sizeof(g_dynamic), "%sを盗まれた！", subject);
        return g_dynamic;
    }
    if (starts_with_ci(text, "Dropped ") || starts_with_ci(text, "dropped ")) {
        p = text + strlen("Dropped ");
        item = translate_item_line(p);
        snprintf(subject, sizeof(subject), "%s", item != NULL ? item : p);
        snprintf(g_dynamic, sizeof(g_dynamic), "%sを落とした。", subject);
        return g_dynamic;
    }
    if (starts_with_ci(text, "Was wearing ") || starts_with_ci(text, "was wearing ")) {
        p = text + strlen("Was wearing ");
        item = translate_item_line(p);
        snprintf(subject, sizeof(subject), "%s", item != NULL ? item : p);
        snprintf(g_dynamic, sizeof(g_dynamic), "%sを脱いだ。", subject);
        return g_dynamic;
    }
    if (starts_with_ci(text, "You used to be ")) {
        p = text + strlen("You used to be ");
        while (*p == ' ') ++p;
        if (starts_with_ci(p, "wearing ")) {
            p += strlen("wearing ");
            item = translate_item_line(p);
            snprintf(subject, sizeof(subject), "%s", item != NULL ? item : p);
            snprintf(g_dynamic, sizeof(g_dynamic), "%sを脱いだ。", subject);
            return g_dynamic;
        }
    }
    if (starts_with_ci(text, "Wearing ") || starts_with_ci(text, "wearing ")) {
        p = text + strlen("Wearing ");
        item = translate_item_line(p);
        snprintf(subject, sizeof(subject), "%s", item != NULL ? item : p);
        snprintf(g_dynamic, sizeof(g_dynamic), "%sを身につけた。", subject);
        return g_dynamic;
    }
    if (starts_with_ci(text, "You are now wearing ")) {
        p = text + strlen("You are now wearing ");
        item = translate_item_line(p);
        snprintf(subject, sizeof(subject), "%s", item != NULL ? item : p);
        snprintf(g_dynamic, sizeof(g_dynamic), "%sを身につけた。", subject);
        return g_dynamic;
    }
    if (starts_with_ci(text, "Wielding ") || starts_with_ci(text, "wielding ")) {
        p = text + strlen("Wielding ");
        item = translate_item_line(p);
        snprintf(subject, sizeof(subject), "%s", item != NULL ? item : p);
        snprintf(g_dynamic, sizeof(g_dynamic), "%sを武器にした。", subject);
        return g_dynamic;
    }
    if (starts_with_ci(text, "You are now wielding ")) {
        p = text + strlen("You are now wielding ");
        item = translate_item_line(p);
        snprintf(subject, sizeof(subject), "%s", item != NULL ? item : p);
        snprintf(g_dynamic, sizeof(g_dynamic), "%sを武器にした。", subject);
        return g_dynamic;
    }
    if (starts_with_ci(text, "The ") && strstr(text, " appears confused") != NULL) {
        const char *end = strstr(text, " appears confused");
        size_t n = (size_t)(end - (text + 4));
        if (n < sizeof(subject)) {
            memcpy(subject, text + 4, n); subject[n] = 0;
            p = translate_subject(subject, translated, sizeof(translated));
            snprintf(g_dynamic, sizeof(g_dynamic), "%sは混乱したようだ。", p);
            return g_dynamic;
        }
    }
    if (starts_with_ci(text, "You are frozen by the ")) {
        p = text + strlen("You are frozen by the ");
        p = translate_subject(p, translated, sizeof(translated));
        snprintf(g_dynamic, sizeof(g_dynamic), "%sの力で体が凍りついた。", p);
        return g_dynamic;
    }
    if (starts_with_ci(text, "Your ") && strstr(text, " glows ") != NULL) {
        const char *glows = strstr(text, " glows ");
        size_t n = (size_t)(glows - (text + 5));
        if (n < sizeof(subject)) {
            memcpy(subject, text + 5, n); subject[n] = 0;
            p = translate_subject(subject, translated, sizeof(translated));
            snprintf(g_dynamic, sizeof(g_dynamic), "%sが一瞬輝いた。", p);
            return g_dynamic;
        }
    }
    if (starts_with_ci(text, "This scroll is an ") && ends_with(text, " scroll")) {
        size_t n = strlen(text) - strlen("This scroll is an ") - strlen(" scroll");
        if (n < sizeof(subject)) {
            memcpy(subject, text + strlen("This scroll is an "), n); subject[n] = 0;
            p = translate_subject(subject, translated, sizeof(translated));
            snprintf(g_dynamic, sizeof(g_dynamic), "これは%sの巻き物だ。", p);
            return g_dynamic;
        }
    }
    if (starts_with_ci(text, "The room is lit by a shimmering ") && ends_with(text, " light")) {
        size_t n = strlen(text) - strlen("The room is lit by a shimmering ") - strlen(" light");
        if (n < sizeof(subject)) {
            memcpy(subject, text + strlen("The room is lit by a shimmering "), n); subject[n] = 0;
            p = translate_subject(subject, translated, sizeof(translated));
            snprintf(g_dynamic, sizeof(g_dynamic), "部屋がきらめく%sの光に照らされた。", p);
            return g_dynamic;
        }
    }
    if (starts_with_ci(text, "The ") && strstr(text, " whizzes by you") != NULL) {
        const char *end = strstr(text, " whizzes by you");
        size_t n = (size_t)(end - (text + 4));
        if (n < sizeof(subject)) {
            memcpy(subject, text + 4, n); subject[n] = 0;
            p = translate_subject(subject, translated, sizeof(translated));
            snprintf(g_dynamic, sizeof(g_dynamic), "%sがすぐそばを飛び過ぎた。", p);
            return g_dynamic;
        }
    }
    if (starts_with_ci(text, "The ") && strstr(text, " whizzes past ") != NULL) {
        const char *mid = strstr(text, " whizzes past ");
        size_t n = (size_t)(mid - (text + 4));
        if (n < sizeof(subject)) {
            memcpy(subject, text + 4, n); subject[n] = 0;
            p = translate_subject(subject, translated, sizeof(translated));
            snprintf(g_dynamic, sizeof(g_dynamic), "%sが標的のそばを飛び過ぎた。", p);
            return g_dynamic;
        }
    }
    if (starts_with_ci(text, "You are already wearing some")) {
        return "すでによろいを着ている。先に脱ぐ必要がある。";
    }
    if (starts_with_ci(text, "You can't wear that")) return "それはよろいとして着られない。";
    if (starts_with_ci(text, "You can't wield armor")) return "よろいを武器として持つことはできない。";
    if (starts_with_ci(text, "You can't wield ")) {
        p = text + strlen("You can't wield ");
        item = translate_item_line(p);
        snprintf(subject, sizeof(subject), "%s", item != NULL ? item : p);
        snprintf(g_dynamic, sizeof(g_dynamic), "%sを武器にすることはできない。", subject);
        return g_dynamic;
    }

    item = translate_item_line(text);
    if (item != NULL) return item;

    /* A trap name or monster name can be printed by itself. */
    p = name_exact(text);
    if (p != NULL) return p;

    return NULL;
}

static int contains_ascii_word(const char *text);

/* 末尾の . と ! を除いた長さ。 */
static size_t length_without_end_marks(const char *text)
{
    size_t n = strlen(text);
    while (n > 0 && (text[n - 1] == '.' || text[n - 1] == '!')) --n;
    return n;
}

static int ends_with_sentence_mark(const char *ja)
{
    const size_t n = strlen(ja);
    if (n == 0) return 0;
    if (ja[n - 1] == '.' || ja[n - 1] == '!' || ja[n - 1] == '?') return 1;
    return n >= 3 && (memcmp(ja + n - 3, "\xe3\x80\x82", 3) == 0      /* 。 */
                      || memcmp(ja + n - 3, "\xef\xbc\x81", 3) == 0   /* ！ */
                      || memcmp(ja + n - 3, "\xef\xbc\x9f", 3) == 0); /* ？ */
}

/*
 * 末尾の . と ! の有無だけが違う文を同じ文として引く（大文字小文字は無視）。Rogue 5.4 は 3.6 と同じ文を
 * 小文字で始め、末尾の . を付けない（3.6 "Oh, now this scroll has a map on it." / 5.4 "oh, now this scroll has a
 * map on it"）。持ち物の行も inv_name が末尾に . を足す（"The Amulet of Yendor."）。原文に . や ! があって
 * 訳の末尾に句点などが無いときは、原文の末尾をそのまま付ける。
 */
static const char *lookup_table_loose(const rl_translation *table, size_t count, const char *text)
{
    static char joined[1024];
    const size_t text_n = length_without_end_marks(text);
    size_t i;
    if (text_n == 0) return NULL;
    for (i = 0; i < count; ++i) {
        const size_t key_n = length_without_end_marks(table[i].en);
        if (key_n != text_n || !ascii_ieq_n(text, table[i].en, text_n)) continue;
        if (text[text_n] != '\0' && !ends_with_sentence_mark(table[i].ja)
            && strlen(table[i].ja) + strlen(text + text_n) < sizeof(joined)) {
            snprintf(joined, sizeof(joined), "%s%s", table[i].ja, text + text_n);
            return joined;
        }
        return table[i].ja;
    }
    return NULL;
}

static const char *lookup(const char *text)
{
    static char trimmed[1024];
    static char padded[1024];
    const char *exact;
    const char *dynamic;
    size_t length;
    size_t spaces = 0;
    if (text == NULL) return "";
    exact = lookup_table(kTranslations, sizeof(kTranslations) / sizeof(kTranslations[0]), text);
    if (exact != NULL) return exact;
    dynamic = translate_dynamic(text);
    if (dynamic != NULL) return dynamic;
    /* 動的な規則に当たらなかった文だけ、末尾の . ! と大文字小文字の違いを許して表を引く（5.4 の綴り）。 */
    exact = lookup_table_loose(kTranslations, sizeof(kTranslations) / sizeof(kTranslations[0]), text);
    if (exact != NULL) return exact;
    /*
     * 原作は同じ文を「末尾に空白あり／なし」の両方で書く（"Which direction? " と
     * "Which direction?"）。表に片方しか無いと、画面ではそのまま英語が出る。
     * 末尾の空白だけを外して引き直し、当たったら同じ数の空白を戻す。
     */
    /*
     * printf で改行ごと 1 本に書かれた字（3.6 のスコア画面の
     * "Top Ten Adventurers:\nRank\tScore\tName\n"）は、行ごとに訳してつなぐ。
     */
    if (strchr(text, '\n') != NULL && strlen(text) < sizeof(trimmed)) {
        static char joined[2048];
        char line[1024];
        const char *p = text;
        size_t used = 0;
        int any = 0;
        joined[0] = '\0';
        while (*p != '\0') {
            const char *nl = strchr(p, '\n');
            const size_t n = nl != NULL ? (size_t)(nl - p) : strlen(p);
            const char *piece;
            memcpy(line, p, n);
            line[n] = '\0';
            piece = n > 0 ? lookup(line) : line;
            if (piece != line) any = 1;
            if (used + strlen(piece) + 2 >= sizeof(joined)) return text;
            strcpy(joined + used, piece);
            used += strlen(piece);
            if (nl == NULL) break;
            joined[used++] = '\n';
            joined[used] = '\0';
            p = nl + 1;
        }
        return any ? joined : text;
    }
    length = strlen(text);
    while (length > spaces && text[length - spaces - 1] == ' ') ++spaces;
    if (spaces == 0 || length - spaces == 0 || length >= sizeof(trimmed)) return text;
    memcpy(trimmed, text, length - spaces);
    trimmed[length - spaces] = '\0';
    exact = lookup_table(kTranslations, sizeof(kTranslations) / sizeof(kTranslations[0]), trimmed);
    if (exact == NULL) exact = translate_dynamic(trimmed);
    if (exact == NULL) return text;
    if (strlen(exact) + spaces >= sizeof(padded)) return exact;
    strcpy(padded, exact);
    memset(padded + strlen(exact), ' ', spaces);
    padded[strlen(exact) + spaces] = '\0';
    return padded;
}

/*
 * 訳が当たらなかった字を書き出す（開発用）。環境変数 RL_I18N_LOG にパスを入れて
 * 遊ぶと、実際に画面へ出た字のうち英語のまま残ったものだけが 1 行ずつ溜まる。
 * 静的な抜き出しでは、addmsg で 2 つに割れた断片しか見えない——組み上がった 1 文は
 * ここを通るときにしか存在しない。
 */
static void log_untranslated(const char *text)
{
    static FILE *log;
    static int checked;
    if (!checked) {
        const char *path = getenv("RL_I18N_LOG");
        checked = 1;
        if (path != NULL && *path != '\0') log = fopen(path, "a");
    }
    if (log == NULL) return;
    if (!contains_ascii_word(text)) return;
    fprintf(log, "%s\n", text);
    fflush(log);
}

static volatile int g_english;

void rl_i18n_set_english(int english) { g_english = english ? 1 : 0; }
int rl_i18n_is_english(void) { return g_english; }

const char *rl_ja_text(const char *text)
{
    const char *result;
    if (g_english) return text != NULL ? text : "";
    result = lookup(text);
    if (text != NULL && result == text) log_untranslated(text);
    return result;
}

static int contains_ascii_word(const char *text)
{
    int run = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (*p != 0) {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
            if (++run >= 3) return 1;
        } else {
            run = 0;
        }
        ++p;
    }
    return 0;
}

int rl_i18n_selftest(void)
{
    static const char *cases[] = {
        "You used to be  wearing b) +1 ring mail",
        "You are now wearing b) +1 ring mail",
        "You are now wielding c) +1 mace",
        "You now have d) a potion of healing",
        "You have defeated the kobold.",
        "You found 37 gold pieces.",
        "The kobold appears confused.",
        "You are frozen by the ice monster",
        "The room is lit by a shimmering blue light",
        "You are hit by the magic missile",
        "A small dart just hit you in the shoulder",
        "There is nothing here to pick up",
        /* 2026-09-23: 画面で英語のまま残っていた形。 */
        "Top Ten Adventurers:\nRank\tScore\tName\n",
        "Which direction? ",
        "b\tdown & left",
        "t<dir>\tthrow something",
        "'u': unknown character",
        "Please type one of !?=/ (ESCAPE to quit)",
        "You feel very sick now",
        "The jackal doesn't hit you",
        "You now have a scroll titled 'muryumi lechod' (f)",
        /* rogue_restore 第 2 回・項目 3: 好きな果物の既定の日本語名
           「スライムモールド」を拾ったときの行（things.c の単数形 "A%s %s"）。 */
        "You now have a "
        "\xe3\x82\xb9\xe3\x83\xa9\xe3\x82\xa4\xe3\x83\xa0\xe3\x83\xa2\xe3\x83\xbc\xe3\x83\xab\xe3\x83\x89 (c)",
        /* 同・複数形（things.c の "%d %ss"）。原作がそのまま貼ると
           「スライムモールドs」になる（strip_glued_plural_s で外す）。 */
        "You now have 2 "
        "\xe3\x82\xb9\xe3\x83\xa9\xe3\x82\xa4\xe3\x83\xa0\xe3\x83\xa2\xe3\x83\xbc\xe3\x83\xab\xe3\x83\x89s (c)",
        /* 修-47：種類の語が行頭に来る持ち物の行（冠詞を取ると "scroll of …" になり、直前の空白を要求していた置き換えが外れた）。 */
        "A scroll of magic mapping.",
        "2 scrolls of create monster.",
        "d) a scroll of identify",
        "A potion of healing(red).",
        "A staff of magic missile(cherry).",
        "A wand of light(oaken).",
        "A +1 ring of protection(jade).",
        "A jade ring.",
        "A rock.",
        "3 crossbow bolts.",
        "The Amulet of Yendor.",
        /* 同・材質（原作 init.c の wood[]・metal[]・stones[] のうち表に無かった語）。 */
        "A zebra wood staff.",
        "A eucalyptus wand.",
        "A fall wand.",
        "A lead staff.",
        "A kryptonite ring.",
        /* 修-47：5.4 の文は 3.6 と同じ文を小文字で始め、末尾の . を付けない。 */
        "oh, now this scroll has a map on it",
        "a small dart whizzes by your ear and vanishes",
        "warning, connectivity problem on this level",
        "The scroll turns to dust as you pick it up.",
        /* 杖の使用回数・地面で消える文・幻覚中に浮遊が切れた文。 */
        "A staff of light [10 charges](cherry).",
        "Your dagger vanishes as it hits the ground.",
        "a dart vanishes as it hits the ground.",
        "the dagger vanishes as it hits the ground",
        "bummer!  You've hit the ground",
    };
    size_t i;
    {
        /* 経験レベルの上昇を「階」と訳していた（2026-09-23 に遊ぶ人が指摘）。 */
        const char *level_up = rl_ja_text("Welcome to level 2");
        if (level_up == NULL || strstr(level_up, "階") != NULL || strstr(level_up, "レベル2") == NULL) {
            fprintf(stderr, "i18n selftest level-up: %s\n", level_up != NULL ? level_up : "(null)");
            return 0;
        }
    }
    {
        /* 外したのに「当てた」と訳していた（2026-09-23 に遊ぶ人が指摘）。 */
        const char *miss = rl_ja_text("The jackal doesn't hit you");
        if (miss == NULL || strstr(miss, "外した") == NULL) {
            fprintf(stderr, "i18n selftest doesn't-hit: %s\n", miss != NULL ? miss : "(null)");
            return 0;
        }
    }
    {
        /* rogue_restore 第 2 回・項目 3: 好きな果物の既定の日本語名を複数個
           拾ったとき、原作が貼った "s" が残って「スライムモールドs」の
           まま英日混在にならないこと（strip_glued_plural_s）。 */
        const char *picked_up = rl_ja_text(
            "You now have 2 "
            "\xe3\x82\xb9\xe3\x83\xa9\xe3\x82\xa4\xe3\x83\xa0\xe3\x83\xa2\xe3\x83\xbc\xe3\x83\xab\xe3\x83\x89s (c)");
        if (picked_up == NULL || strstr(picked_up, "\xe3\x83\x89s") != NULL
            || strstr(picked_up, "手に入れた") == NULL) {
            fprintf(stderr, "i18n selftest fruit-plural: %s\n", picked_up != NULL ? picked_up : "(null)");
            return 0;
        }
    }
    /* 英語の表示（修-46）：原作の英語をそのまま返し、日本語に変えない。戻すと訳が当たる。 */
    {
        const char *sentence = "You found 37 gold pieces.";
        rl_i18n_set_english(1);
        if (rl_ja_text(sentence) != sentence || rl_ja_format("Which direction? ") == NULL
            || strcmp(rl_ja_text("Which direction? "), "Which direction? ") != 0 || !rl_i18n_is_english()) {
            rl_i18n_set_english(0);
            fprintf(stderr, "i18n selftest english: text was changed\n");
            return 0;
        }
        rl_i18n_set_english(0);
        if (strcmp(rl_ja_text(sentence), sentence) == 0 || rl_i18n_is_english()) {
            fprintf(stderr, "i18n selftest english: japanese did not come back\n");
            return 0;
        }
    }
    {
        /* 修-47：種類の語を含むだけの文は、持ち物の行ではないので部分的に置き換えない（表に無ければ原文のまま）。 */
        static const char *const sentences[] = {
            "you hear the ring of a distant bell",
            "now this scroll has a map and a potion",
            "Your staff turns to dust.",
            "lead the wand, fall to the rock, and scroll on it",
        };
        for (i = 0; i < sizeof(sentences) / sizeof(sentences[0]); ++i) {
            const char *left_alone = rl_ja_text(sentences[i]);
            if (left_alone == NULL || strcmp(left_alone, sentences[i]) != 0) {
                fprintf(stderr, "i18n selftest sentence %lu was changed: %s => %s\n",
                    (unsigned long)i, sentences[i], left_alone != NULL ? left_alone : "(null)");
                return 0;
            }
        }
    }
    {
        /* 5.4 で同時に出る色名は、別の語に訳す（plaid が aquamarine と同じ「水色」だった）。 */
        char first[256];
        char vanish[256];
        snprintf(first, sizeof(first), "%s", rl_ja_text("An aquamarine potion."));
        if (strcmp(first, rl_ja_text("A plaid potion.")) == 0) {
            fprintf(stderr, "i18n selftest colors: aquamarine and plaid look the same: %s\n", first);
            return 0;
        }
        /* 地面で消える文を、戦闘の " hit "（攻撃を当てた）と取り違えない。 */
        snprintf(vanish, sizeof(vanish), "%s", rl_ja_text("Your dagger vanishes as it hits the ground."));
        if (strstr(vanish, "\xe6\x94\xbb\xe6\x92\x83") != NULL) {
            fprintf(stderr, "i18n selftest vanish: %s\n", vanish);
            return 0;
        }
    }
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const char *translated = rl_ja_text(cases[i]);
        if (translated == NULL || translated == cases[i] || strcmp(translated, cases[i]) == 0
            || contains_ascii_word(translated)) {
            fprintf(stderr, "i18n selftest case %lu: %s => %s\n",
                (unsigned long)i, cases[i], translated != NULL ? translated : "(null)");
            return 0;
        }
    }
    return 1;
}

/*
 * Direct printw()/printf() calls can safely translate their format string: the
 * formatting buffer lives in the adapter and is large enough for UTF-8.  The
 * game's own msgbuf is deliberately not touched; it remains byte-compatible
 * English and is translated by rl_ja_text() only at draw time.
 */
const char *rl_ja_format(const char *format)
{
    const char *exact;
    if (format == NULL) return "";
    if (g_english) return format;
    exact = lookup_table(kTranslations, sizeof(kTranslations) / sizeof(kTranslations[0]), format);
    return exact != NULL ? exact : format;
}
