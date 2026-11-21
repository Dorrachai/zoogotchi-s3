#pragma once
#include "../study_decks.h"

// =============================================================================
// 4. Japanese JLPT (N4 - N1) Grammar, Keigo & Vocab (28 cards)
// =============================================================================
static const StudyCard kDeckLangJp[] = {
    {"[N4] 'Kagi wo otoshite ___.' Best fit for accidental drop?", {"okimashita", "shimatta", "mimashita", "ikimashita"}, 1, "~te shimau marks regret, accident, or full completion.", "[N4] ~te shimau"},
    {"[N4] 'Mainichi renshuu shite, hanaseru ___.' Choose change in ability:", {"koto ni shita", "hazu datta", "you ni natta", "mono da"}, 2, "V-potential + you ni naru = reached a new state/ability.", "[N4] ~you ni naru"},
    {"[N4] 'Doa ga katte ni ___!' Choose the intransitive verb:", {"aketa", "aita", "shimeru", "shimeta"}, 1, "Aku (ga) is intransitive (opened itself); akeru (wo) is transitive.", "[N4] aku vs akeru"},
    {"[N4] 'Kare wa kinoo tsuita ___ da.' Logical expectation based on facts?", {"tsumori", "mama", "bakari", "hazu"}, 3, "~hazu da = objective/logical expectation backed by evidence.", "[N4] ~hazu da"},
    {"[N3] 'Kirai na ___ ja nai ga, ima isogashii.' Soften partial negation:", {"hazu", "wake", "kagiri", "mono"}, 1, "~wake de wa nai = not necessarily / does not mean that.", "[N3] ~wake dewanai"},
    {"[N3] 'Raigetsu Oosaka e tenkin suru ___.' Rule/company decided it:", {"koto ni shita", "you ni shita", "koto ni natta", "wake ni natta"}, 2, "~koto ni naru = external decision; ~koto ni suru = personal choice.", "[N3] koto ni naru"},
    {"[N3] 'Jiko no ___ densha ga okureta.' Blame a negative outcome on cause:", {"sei de", "okage de", "tame ni", "kuse ni"}, 0, "~sei de = fault/blame for bad result; ~okage de = thanks to (good).", "[N3] ~sei vs ~okage"},
    {"[N3] 'Kasa ga nurete iru. Soto wa ame ___.' Strong speaker conviction:", {"wake ga nai", "koto ga aru", "ka mo shirenai", "ni chigai nai"}, 3, "~ni chigai nai = must be / no doubt about it (strong certainty).", "[N3] ni chigai nai"},
    {"[N3] 'Renshuu sureba suru ___ jouzu ni naru.' Express proportional rise:", {"dake", "hodo", "kurai", "mama"}, 1, "A-ba A-hodo = The more A, the more B (proportional correlation).", "[N3] ~ba ~hodo"},
    {"[N3] 'Wasurenai ___ memo shita.' Express goal with non-volitional verb:", {"tame ni", "kuse ni", "you ni", "wari ni"}, 2, "Negative/potential verbs take ~you ni; volitional V-dict takes ~tame ni.", "[N3] you vs tame"},
    {"[N3] 'Fuyu wa kaze wo hiki-___ da.' Express frequent unwanted tendency:", {"gachi", "ppoi", "kake", "gatai"}, 0, "V-stem/N + gachi = prone to / tends to happen (often negative).", "[N3] ~gachi"},
    {"[N3] 'Denki wo tsuke-___ de neta.' Action left undone/improperly open:", {"tsuzuke", "nagara", "mama", "ppanashi"}, 3, "V-stem + ppanashi = left something in an unfinished/careless state.", "[N3] ~ppanashi"},
    {"[N2] 'Yakusoku shita kara, yasumu ___.' Cannot do due to social duty:", {"wake ga nai", "wake ni wa ikanai", "koto wa nai", "hazu ga nai"}, 1, "~wake ni wa ikanai = cannot bring oneself/morally unable to do X.", "[N2] wake ni ikanai"},
    {"[N2] 'Kasa wo wasureta hi ___ ame ga furu.' Unfortunate irony ('just when'):", {"ni tsurete", "wo motte", "ni kagitte", "ni kakete"}, 2, "~ni kagitte = ironically on that very day/person (Murphy's law sense).", "[N2] ~ni kagitte"},
    {"[N2] 'Katta ___ no, ichido mo tsukatte inai.' Concessive 'although':", {"koto", "wake", "hazu", "mono"}, 3, "~mono no = Although / Even though X is true, unexpectedly Y.", "[N2] ~mono no"},
    {"[N2] 'Shimekiri da, tetsuya se-___.' Reluctantly have no choice but to:", {"zaru wo enai", "wake ni ikanai", "koto ni naru", "mono ka"}, 0, "V-nai (suru->se) + zaru wo enai = compelled to, cannot help doing.", "[N2] ~zaru wo enai"},
    {"[N2] 'Toshi wo toru ___ kioku ga usureru.' Co-varying gradual change:", {"ni taishite", "ni tsurete", "ni totte", "wo megutte"}, 1, "~ni tsurete = As X changes, Y correspondingly shifts with it.", "[N2] ~ni tsurete"},
    {"[N2] 'Mada jikan ga aru node isogu ___.' Meaning: no need to hurry:", {"wake ga nai", "mono de wa nai", "koto wa nai", "hazu ga nai"}, 2, "V-dict + koto wa nai = There is no need / necessity to do V.", "[N2] ~koto wa nai"},
    {"[N2] 'Junbi ga deki ___ shuppatsu shimasu.' Formal 'as soon as':", {"kagiri", "totan", "uchi ni", "shidai"}, 3, "V-stem + shidai = immediately upon / as soon as (formal business).", "[N2] ~shidai"},
    {"[N2] 'Sonnanode wa jiko wo okoshi-___.' Risk of an undesirable event:", {"kanenai", "kaneru", "gatai", "kirenai"}, 0, "V-stem + kanenai = likely/capable of causing a bad outcome!", "[N2] ~kanenai"},
    {"[N2] Keigo: 'Watashi ga goshounin ___.' Humble form of 'suru' (I do):", {"nasaimasu", "itashimasu", "irasshaimasu", "osshaimasu"}, 1, "Itasu = Kenjougo (humble); Nasaru = Sonkeigo (honorific for other).", "[N2] Keigo: itasu"},
    {"[N2] Keigo to client: 'Douzo o-___ kudasai.' Honorific 'eat/drink':", {"itadaki", "ukagai", "meshiagari", "mairi"}, 2, "Meshiagaru = Sonkeigo (you eat); Itadaku = Kenjougo (I eat/receive).", "[N2] Keigo: eat"},
    {"[N2] Vocab: 'Kare no setsumei ni wa ___ ga aru.' Self-contradiction:", {"konkyo", "kouka", "gokai", "mujun"}, 3, "Mujun = contradiction (from the shield-and-spear idiom).", "[N2] Mujun"},
    {"[N2] Vocab: 'Futatsu no an de ___ ten wo sagasu.' Mutual compromise:", {"dakyou", "kyouchou", "hihan", "shikkou"}, 0, "Dakyou = compromise, finding middle ground in negotiations.", "[N2] Dakyou"},
    {"[N1] 'Arashi wo ___ to mo sezu, fune wa susunda.' Defying/disregarding:", {"koto", "mono", "tokoro", "tame"}, 1, "N + wo mono to mo sezu = Paying no heed to hardships; dauntlessly.", "[N1] mono to mo sezu"},
    {"[N1] 'Ano bujoku wa gaman ni ___.' Literary 'unbearable / cannot endure':", {"ataru", "oyobanai", "taenai", "shikanai"}, 2, "N/V + ni taenai = intolerable (if negative N) or deeply worthy (if good).", "[N1] ~ni taenai"},
    {"[N1] 'Saigo no shiai ___ kaijou wa manin datta.' Special occasion cause:", {"to iedo", "ni kakete", "bakari ni", "to atte"}, 3, "~to atte = precisely because it is a rare/special occasion, Y happens.", "[N1] ~to atte"},
    {"[N1] 'Kesshou ni susumete ureshii ___ da.' Deepest emotion ('extremely'):", {"kagiri", "bakari", "shidai", "toori"}, 0, "Adj-i/Adj-na + kagiri da = expresses the utmost degree of personal feeling.", "[N1] ~kagiri da"},
};
