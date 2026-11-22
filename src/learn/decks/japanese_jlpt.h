#pragma once
#include "../study_decks.h"

// =============================================================================
// 4. Japanese JLPT (N4 - N1) Grammar, Keigo & Vocab (28 cards, JP + Furigana)
// =============================================================================
static const StudyCard kDeckLangJp[] = {
    {"[N4] 鍵(かぎ)を落(お)として___。 (regret / accident)", {"おきました", "しまった", "みました", "いきました"}, 1, "〜てしまう = regret, accident, or full completion.", "[N4] 〜てしまう"},
    {"[N4] 毎日(まいにち)練習(れんしゅう)して、話(はな)せる___。 (new ability)", {"ことにした", "はずだった", "ようになった", "ものだ"}, 2, "V-potential + ようになる = reached a new ability/state.", "[N4] 〜ようになる"},
    {"[N4] ドアが勝手(かって)に___！ (intransitive verb)", {"開(あ)けた", "開(あ)いた", "閉(し)める", "閉(し)めた"}, 1, "開(あ)く(が) is intransitive; 開(あ)ける(を) is transitive.", "[N4] 開(あ)く vs 開(あ)ける"},
    {"[N4] 彼(かれ)は昨日(きのう)着(つ)いた___だ。 (logical expectation)", {"つもり", "まま", "ばかり", "はず"}, 3, "〜はずだ = logical expectation backed by evidence.", "[N4] 〜はずだ"},
    {"[N3] 嫌(きら)いな___じゃないが、今(いま)忙(いそが)しい。 (partial negation)", {"はず", "わけ", "限(かぎ)り", "もの"}, 1, "〜わけではない = not necessarily / does not mean that.", "[N3] 〜わけではない"},
    {"[N3] 来月(らいげつ)大阪(おおさか)へ転勤(てんきん)する___。 (company decided)", {"ことにした", "ようにした", "ことになった", "わけになった"}, 2, "〜ことになる = external decision; 〜ことにする = own choice.", "[N3] 〜ことになる"},
    {"[N3] 事故(じこ)の___電車(でんしゃ)が遅(おく)れた。 (blame cause)", {"せいで", "おかげで", "ために", "くせに"}, 0, "〜せいで = blame for bad result; 〜おかげで = thanks to.", "[N3] せいで vs おかげで"},
    {"[N3] 傘(かさ)が濡(ぬ)れている。外(そと)は雨(あめ)___。 (strong certainty)", {"わけがない", "ことがある", "かもしれない", "に違(ちが)いない"}, 3, "〜に違(ちが)いない = must be / no doubt about it.", "[N3] 〜に違(ちが)いない"},
    {"[N3] 練習(れんしゅう)すればする___上手(じょうず)になる。 (the more...)", {"だけ", "ほど", "くらい", "まま"}, 1, "AばAほど = The more A, the more B (proportional rise).", "[N3] 〜ば〜ほど"},
    {"[N3] 忘(わす)れない___メモした。 (goal w/ non-volitional V)", {"ために", "くせに", "ように", "わりに"}, 2, "Negative/potential takes 〜ように; volitional takes 〜ために.", "[N3] ように vs ために"},
    {"[N3] 冬(ふゆ)は風邪(かぜ)をひき___だ。 (unwanted tendency)", {"がち", "っぽい", "かけ", "がたい"}, 0, "V-stem/N + がち = prone to / tends to happen (negative).", "[N3] 〜がち"},
    {"[N3] 電気(でんき)をつけ___で寝(ね)た。 (left carelessly on)", {"続(つづ)け", "ながら", "まま", "っぱなし"}, 3, "V-stem + っぱなし = left unfinished / carelessly open.", "[N3] 〜っぱなし"},
    {"[N2] 約束(やくそく)したから、休(やす)む___。 (moral duty)", {"わけがない", "わけにはいかない", "ことはない", "はずがない"}, 1, "〜わけにはいかない = morally/socially unable to do X.", "[N2] 〜わけにはいかない"},
    {"[N2] 傘(かさ)を忘(わす)れた日(ひ)___雨(あめ)が降(ふ)る。 (ironic 'just when')", {"につれて", "をもって", "に限(かぎ)って", "にかけて"}, 2, "〜に限(かぎ)って = ironically on that very day (Murphy's law).", "[N2] 〜に限(かぎ)って"},
    {"[N2] 買(か)った___の、一度(いちど)も使(つか)っていない。 (although)", {"こと", "わけ", "はず", "もの"}, 3, "〜ものの = Although / Even though X is true, unexpectedly Y.", "[N2] 〜ものの"},
    {"[N2] 締切(しめきり)だ、徹夜(てつや)せ___。 (compelled to do)", {"ざるを得(え)ない", "わけにはいかない", "ことになる", "ものか"}, 0, "V-nai(する→せ) + ざるを得(え)ない = reluctantly have no choice.", "[N2] 〜ざるを得(え)ない"},
    {"[N2] 年(とし)を取(と)る___記憶(きおく)が薄(うす)れる。 (co-varying shift)", {"に対(たい)して", "につれて", "にとって", "をめぐって"}, 1, "〜につれて = As X changes, Y gradually shifts with it.", "[N2] 〜につれて"},
    {"[N2] まだ時間(じかん)があるので急(いそ)ぐ___。 (no need to)", {"わけがない", "ものではない", "ことはない", "はずがない"}, 2, "V-dict + ことはない = There is no need / necessity to do V.", "[N2] 〜ことはない"},
    {"[N2] 準備(じゅんび)ができ___出発(しゅっぱつ)します。 (formal 'as soon as')", {"限(かぎ)り", "とたん", "うちに", "次第(しだい)"}, 3, "V-stem + 次第(しだい) = immediately upon / as soon as (formal).", "[N2] 〜次第(しだい)"},
    {"[N2] それでは事故(じこ)を起(お)こし___。 (risk of bad event)", {"かねない", "かねる", "がたい", "きれない"}, 0, "V-stem + かねない = likely/capable of causing a bad outcome!", "[N2] 〜かねない"},
    {"[N2] 敬語(けいご): 私(わたし)が確認(かくにん)___。 (humble 'I do')", {"なさいます", "いたします", "いらっしゃいます", "おっしゃいます"}, 1, "いたす = humble (謙譲語(けんじょうご)); なさる = honorific.", "[N2] 敬語: いたす"},
    {"[N2] 敬語(けいご): どうぞお___ください。 (honorific 'eat/drink')", {"いただき", "うかがい", "召(め)し上(あ)がり", "まいり"}, 2, "召(め)し上(あ)がる = honorific eat; いただく = humble eat.", "[N2] 敬語: 召し上がる"},
    {"[N2] 語彙(ごい): 彼(かれ)の説明(せつめい)には___がある。 (contradiction)", {"根拠(こんきょ)", "効果(こうか)", "誤解(ごかい)", "矛盾(むじゅん)"}, 3, "矛盾(むじゅん) = self-contradiction (shield-and-spear idiom).", "[N2] 矛盾(むじゅん)"},
    {"[N2] 語彙(ごい): 二(ふた)つの案(あん)で___点(てん)を探(さが)す。 (compromise)", {"妥協(だきょう)", "強調(きょうちょう)", "批判(ひはん)", "執行(しっこう)"}, 0, "妥協(だきょう) = compromise, finding middle ground.", "[N2] 妥協(だきょう)"},
    {"[N1] 嵐(あらし)を___ともせず、船(ふね)は進(すす)んだ。 (defying)", {"こと", "もの", "ところ", "ため"}, 1, "N + をものともせず = Paying no heed to hardships; dauntlessly.", "[N1] 〜をものともせず"},
    {"[N1] あの侮辱(ぶじょく)は我慢(がまん)に___。 (unbearable)", {"当(あ)たる", "及(およ)ばない", "堪(た)えない", "しかない"}, 2, "N/V + に堪(た)えない = intolerable / cannot endure (literary).", "[N1] 〜に堪(た)えない"},
    {"[N1] 最後(さいご)の試合(しあい)___会場(かいじょう)は満員(まんいん)だった。 (special occasion)", {"といえど", "にかけて", "ばかりに", "とあって"}, 3, "〜とあって = precisely because it is a rare/special occasion.", "[N1] 〜とあって"},
    {"[N1] 決勝(けっしょう)に進(すす)めて嬉(うれ)しい___だ。 (utmost emotion)", {"限(かぎ)り", "ばかり", "次第(しだい)", "通(とお)り"}, 0, "Adj + 限(かぎ)りだ = expresses utmost degree of emotion.", "[N1] 〜限(かぎ)りだ"},
};
