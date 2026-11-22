#!/usr/bin/env python3
"""
ZooGotchi S3 — LVGL 9.3.0 4-bpp Japanese + Furigana Bitmap Font Generator
Builds src/display/zoo_font_jp_12.c from BIZ UDPGothic (or NotoSansJP/YuGothic)
containing ASCII, Hiragana, Katakana, JP punctuation, and JLPT Kanji.

Usage:
  .venv\\Scripts\\python.exe tools\\build_jp_font.py
  .venv\\Scripts\\python.exe tools\\build_jp_font.py --preview path\\to\\preview.png
"""

import argparse
import re
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUT_C = ROOT / "src" / "display" / "zoo_font_jp_12.c"
JP_DECK_H = ROOT / "src" / "learn" / "decks" / "japanese_jlpt.h"
WEB_HTML = ROOT / "web_ui" / "index.html"
GEN_DECK_PY = ROOT / "tools" / "generate_study_deck.py"

FONT_CANDIDATES = [
    (Path(r"C:\Windows\Fonts\BIZ-UDGothicR.ttc"), 1),
    (Path(r"C:\Windows\Fonts\BIZ-UDGothicR.ttc"), 0),
    (Path(r"C:\Windows\Fonts\NotoSansJP-VF.ttf"), 0),
    (Path(r"C:\Windows\Fonts\YuGothM.ttc"), 0),
    (Path(r"C:\Windows\Fonts\meiryo.ttc"), 0),
]

# Common JLPT N5-N2 kanji (plus whatever appears in firmware/web decks)
COMMON_JLPT_KANJI = (
    "一二三四五六七八九十百千万上下左右中大小長高安新古多少早遅近遠"
    "前後内外東西南北口目耳手足力男女子人名年日月火水木金土曜日週時分"
    "半今午朝昼晩夜春秋冬夏天気雨雪風雲空海山川森木花草犬猫鳥魚虫"
    "見聞言話読書書記語学教習勉強制理解知思考忘覚信疑問答題文文字音"
    "意味事物体心気持感情笑泣怒喜悲楽苦痛怖驚安心心配願望夢希望"
    "行来帰出入会乗降着発出発到着歩走飛泳渡通曲止立座休寝起働作造"
    "使払買売貸借返送届受渡取捨拾集並比選決定変直治直続終始開開閉"
    "押引持運落拾置掛付消点着脱洗掃拭切折破壊修理守助救教育養飼"
    "食飲料理飯朝昼夕夜茶水酒牛乳肉魚野菜果物甘辛苦塩味腹減満"
    "家部屋門戸窓壁床庭台所風呂階建屋店会社校病医院駅空港道橋車電車"
    "船飛行機自転車切符地図旅行切手荷物傘靴服帽子眼鏡時計鍵電気"
    "紙本雑誌新聞手紙電話写真絵映画音楽歌詩劇試合勝負勝負記録結果"
    "国世界日本東京大阪京都外国都市町村地域場所有無存在必要可能不可能"
    "社会経済政治法規則文化歴史自然環境科学技術研究調査報告連絡相談"
    "約束予定準備計画経験機会関係原因理由目的方法手段結果状態変化"
    "程度限際場合頃度回番号種類別違同様同一反対賛成協力許可禁止"
    "注意確認説明紹介案内表現意見提案質問批判比較判断評価選択採用"
    "成功失敗努力苦労我慢耐頑張続徹夜締切転勤事故渋滞遅刻欠席出席"
    "健康病気風邪薬熱咳痛傷治元気疲眠夢記憶薄厚重軽強弱硬柔"
    "白黒赤青黄緑茶色光暗明晴曇涼暖暑寒温冷乾湿濡"
    "私僕彼彼女君誰何僕自分相手友達仲間家族両親父母兄弟姉妹夫婦"
    "先生教師生徒学生医者店員客会社員社長部長課長係"
    "敬謙譲丁寧拝見伺参召上頂存申致仰"
    "矛盾妥協強調執行根拠効果誤解噂嵐侮辱堪満員決勝嬉即皮切"
    "昔最以非未再各毎全半約第定特共公私正誤真偽良悪優劣美醜"
)

OPA4_TABLE = (0, 17, 34, 51, 68, 85, 102, 119, 136, 153, 170, 187, 204, 221, 238, 255)

FONT_SIZE = 12
LINE_HEIGHT = 15
BASE_LINE = 3
CANVAS_W = 32
CANVAS_H = 32
X_ORIG = 6
Y_BASE = 22


def load_source_font() -> ImageFont.FreeTypeFont:
    for path, idx in FONT_CANDIDATES:
        if path.is_file():
            try:
                return ImageFont.truetype(str(path), FONT_SIZE, index=idx)
            except Exception:
                continue
    raise RuntimeError("No Japanese TrueType font found on system.")


def scan_project_non_ascii() -> tuple[set[int], set[int], set[int]]:
    texts = []
    if JP_DECK_H.is_file():
        texts.append(JP_DECK_H.read_text(encoding="utf-8"))
    if WEB_HTML.is_file():
        html = WEB_HTML.read_text(encoding="utf-8")
        m = re.search(r"function loadJlptPack\(\)\{.*?\n\}", html, re.DOTALL)
        if m:
            texts.append(m.group(0))
    if GEN_DECK_PY.is_file():
        texts.append(GEN_DECK_PY.read_text(encoding="utf-8"))

    sym_low = set()
    sym_fullwidth = set()
    kanji = set()
    for txt in texts:
        for ch in txt:
            cp = ord(ch)
            if cp < 0x80:
                continue
            if 0x3041 <= cp <= 0x3096 or 0x30A1 <= cp <= 0x30FC:
                continue
            if 0x4E00 <= cp <= 0x9FFF:
                kanji.add(cp)
            elif 0xFF01 <= cp <= 0xFF5E:
                sym_fullwidth.add(cp)
            elif cp <= 0x303F:
                sym_low.add(cp)
            else:
                raise ValueError(f"Unsupported non-ASCII codepoint U+{cp:04X} ({ch!r}) for font cmaps")
    return sym_low, kanji, sym_fullwidth


def collect_glyph_groups() -> list[dict]:
    sym_low, scanned_kanji, scanned_fw = scan_project_non_ascii()

    base_low = {
        0x00B7, 0x2014, 0x2026, 0x2192,
        0x3000, 0x3001, 0x3002,
        0x300C, 0x300D, 0x300E, 0x300F,
        0x3010, 0x3011, 0x301C,
    }
    sym_low_sorted = sorted(base_low | sym_low)

    fw_sorted = sorted({0xFF01, 0xFF08, 0xFF09, 0xFF1A, 0xFF1F, 0xFF5E} | scanned_fw)

    kanji_all = scanned_kanji | {ord(ch) for ch in COMMON_JLPT_KANJI if 0x4E00 <= ord(ch) <= 0x9FFF}
    kanji_sorted = sorted(kanji_all)

    return [
        {
            "name": "ascii",
            "type": "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY",
            "range_start": 0x0020,
            "range_length": 0x007E - 0x0020 + 1,
            "cps": list(range(0x0020, 0x007F)),
        },
        {
            "name": "symbols",
            "type": "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY",
            "range_start": sym_low_sorted[0],
            "range_length": sym_low_sorted[-1] - sym_low_sorted[0] + 1,
            "cps": sym_low_sorted,
        },
        {
            "name": "hiragana",
            "type": "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY",
            "range_start": 0x3041,
            "range_length": 0x3096 - 0x3041 + 1,
            "cps": list(range(0x3041, 0x3097)),
        },
        {
            "name": "katakana",
            "type": "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY",
            "range_start": 0x30A1,
            "range_length": 0x30FC - 0x30A1 + 1,
            "cps": list(range(0x30A1, 0x30FD)),
        },
        {
            "name": "kanji",
            "type": "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY",
            "range_start": kanji_sorted[0],
            "range_length": kanji_sorted[-1] - kanji_sorted[0] + 1,
            "cps": kanji_sorted,
        },
        {
            "name": "fullwidth_punct",
            "type": "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY",
            "range_start": fw_sorted[0],
            "range_length": fw_sorted[-1] - fw_sorted[0] + 1,
            "cps": fw_sorted,
        },
    ]


def rasterize_glyph(fnt: ImageFont.FreeTypeFont, cp: int) -> tuple[dict, bytes]:
    ch = chr(cp)
    im = Image.new("L", (CANVAS_W, CANVAS_H), 0)
    draw = ImageDraw.Draw(im)
    draw.text((X_ORIG, Y_BASE), ch, font=fnt, fill=255, anchor="ls")

    adv_w_q4 = max(0, int(round(fnt.getlength(ch) * 16)))

    px = im.load()
    nibbles = []
    min_x, min_y = CANVAS_W, CANVAS_H
    max_x, max_y = -1, -1
    for y in range(CANVAS_H):
        for x in range(CANVAS_W):
            n = (px[x, y] * 15 + 127) // 255
            if n > 0:
                if x < min_x:
                    min_x = x
                if y < min_y:
                    min_y = y
                if x + 1 > max_x:
                    max_x = x + 1
                if y + 1 > max_y:
                    max_y = y + 1

    if max_x <= min_x or max_y <= min_y:
        return {"adv_w": adv_w_q4, "box_w": 0, "box_h": 0, "ofs_x": 0, "ofs_y": 0}, b""

    box_w = max_x - min_x
    box_h = max_y - min_y
    ofs_x = min_x - X_ORIG
    ofs_y = Y_BASE - max_y

    for y in range(min_y, max_y):
        for x in range(min_x, max_x):
            nibbles.append((px[x, y] * 15 + 127) // 255)

    # LVGL bpp=4 stride=0 packs nibbles continuously across rows, high nibble first
    packed = bytearray()
    for i in range(0, len(nibbles), 2):
        hi = nibbles[i] & 0x0F
        lo = (nibbles[i + 1] & 0x0F) if (i + 1 < len(nibbles)) else 0
        packed.append((hi << 4) | lo)

    return {
        "adv_w": adv_w_q4,
        "box_w": box_w,
        "box_h": box_h,
        "ofs_x": ofs_x,
        "ofs_y": ofs_y,
    }, bytes(packed)


def generate_c_font(out_c: Path) -> tuple[list[dict], list[dict], bytearray]:
    fnt = load_source_font()
    groups = collect_glyph_groups()

    bitmap_buf = bytearray()
    glyph_dscs = [{"bitmap_index": 0, "adv_w": 0, "box_w": 0, "box_h": 0, "ofs_x": 0, "ofs_y": 0, "cp": 0}]

    for grp in groups:
        grp["glyph_id_start"] = len(glyph_dscs)
        assert grp["range_length"] <= 65535, f"Range too wide for uint16: {grp['name']}"
        for cp in grp["cps"]:
            meta, bm = rasterize_glyph(fnt, cp)
            bidx = len(bitmap_buf)
            bitmap_buf.extend(bm)
            glyph_dscs.append({
                "bitmap_index": bidx,
                "adv_w": meta["adv_w"],
                "box_w": meta["box_w"],
                "box_h": meta["box_h"],
                "ofs_x": meta["ofs_x"],
                "ofs_y": meta["ofs_y"],
                "cp": cp,
            })

    assert len(bitmap_buf) < (1 << 20), f"Bitmap exceeds 20-bit bitmap_index ({len(bitmap_buf)} B)"

    lines = [
        "/*",
        " * ZooGotchi S3 — Auto-generated 4-bpp LVGL 9.3.0 Japanese + ASCII Font",
        f" * Source: {fnt.getname()} | Size: {FONT_SIZE}px | Glyphs: {len(glyph_dscs) - 1} | Bitmap: {len(bitmap_buf)} B",
        " * Regenerate with: python tools/build_jp_font.py",
        " */",
        "",
        "#ifdef LV_LVGL_H_INCLUDE_SIMPLE",
        "    #include \"lvgl.h\"",
        "#else",
        "    #include <lvgl.h>",
        "#endif",
        "",
        "static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {",
    ]

    for off in range(0, len(bitmap_buf), 16):
        chunk = bitmap_buf[off : off + 16]
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    lines.append("};")
    lines.append("")

    lines.append("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {")
    lines.append("    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},")
    for gd in glyph_dscs[1:]:
        lines.append(
            f"    {{.bitmap_index = {gd['bitmap_index']}, .adv_w = {gd['adv_w']}, "
            f".box_w = {gd['box_w']}, .box_h = {gd['box_h']}, "
            f".ofs_x = {gd['ofs_x']}, .ofs_y = {gd['ofs_y']}}}, /* U+{gd['cp']:04X} */"
        )
    lines.append("};")
    lines.append("")

    for grp in groups:
        if grp["type"] == "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY":
            rcps = [cp - grp["range_start"] for cp in grp["cps"]]
            lines.append(f"static const uint16_t unicode_list_{grp['name']}[] = {{")
            for off in range(0, len(rcps), 12):
                lines.append("    " + ", ".join(f"0x{v:04x}" for v in rcps[off : off + 12]) + ",")
            lines.append("};")
            lines.append("")

    lines.append("static const lv_font_fmt_txt_cmap_t cmaps[] = {")
    for grp in groups:
        if grp["type"] == "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY":
            ul = f"unicode_list_{grp['name']}"
            ll = len(grp["cps"])
        else:
            ul = "NULL"
            ll = 0
        lines.extend([
            "    {",
            f"        .range_start = {grp['range_start']}, .range_length = {grp['range_length']}, .glyph_id_start = {grp['glyph_id_start']},",
            f"        .unicode_list = {ul}, .glyph_id_ofs_list = NULL, .list_length = {ll},",
            f"        .type = {grp['type']}",
            "    },",
        ])
    lines.append("};")
    lines.append("")

    lines.extend([
        "static const lv_font_fmt_txt_dsc_t font_dsc = {",
        "    .glyph_bitmap = glyph_bitmap,",
        "    .glyph_dsc = glyph_dsc,",
        "    .cmaps = cmaps,",
        "    .kern_dsc = NULL,",
        "    .kern_scale = 0,",
        f"    .cmap_num = {len(groups)},",
        "    .bpp = 4,",
        "    .kern_classes = 0,",
        "    .bitmap_format = LV_FONT_FMT_TXT_PLAIN,",
        "    .stride = 0",
        "};",
        "",
        "const lv_font_t zoo_font_jp_12 = {",
        "    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,",
        "    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,",
        "    .release_glyph = NULL,",
        f"    .line_height = {LINE_HEIGHT},",
        f"    .base_line = {BASE_LINE},",
        "    .subpx = LV_FONT_SUBPX_NONE,",
        "    .kerning = LV_FONT_KERNING_NONE,",
        "    .static_bitmap = 0,",
        "    .underline_position = -1,",
        "    .underline_thickness = 1,",
        "    .dsc = &font_dsc,",
        "    .fallback = &lv_font_montserrat_12,",
        "    .user_data = NULL",
        "};",
        "",
    ])

    out_c.parent.mkdir(parents=True, exist_ok=True)
    out_c.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(
        f"Generated {out_c} ({len(glyph_dscs) - 1} glyphs across {len(groups)} cmaps, "
        f"bitmap={len(bitmap_buf):,} B)"
    )
    return groups, glyph_dscs, bitmap_buf


def _lookup_gid(groups: list[dict], cp: int) -> int:
    for g in groups:
        rcp = cp - g["range_start"]
        if 0 <= rcp < g["range_length"]:
            if g["type"] == "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY":
                return g["glyph_id_start"] + rcp
            try:
                pos = g["cps"].index(cp)
                return g["glyph_id_start"] + pos
            except ValueError:
                pass
    return 0


def _draw_lvgl_line(im: Image.Image, groups: list[dict], glyph_dscs: list[dict], bitmap_buf: bytearray,
                    x0: int, y0: int, max_w: int, text: str, fg=(30, 41, 59)):
    """Decode and blit LVGL 4-bpp glyphs using exact lv_draw_label.c & lv_font_fmt_txt.c math."""
    x = x0
    y = y0
    px = im.load()
    for ch in text:
        gid = _lookup_gid(groups, ord(ch))
        if not gid:
            continue
        gd = glyph_dscs[gid]
        adv = (gd["adv_w"] + 8) >> 4
        if x - x0 + adv > max_w:
            x = x0
            y += LINE_HEIGHT + 1
        gx = x + gd["ofs_x"]
        gy = y + (LINE_HEIGHT - BASE_LINE) - gd["box_h"] - gd["ofs_y"]
        bidx = gd["bitmap_index"]
        nibble_i = 0
        byte_p = bidx
        for row in range(gd["box_h"]):
            for col in range(gd["box_w"]):
                if (nibble_i & 1) == 0:
                    nib = bitmap_buf[byte_p] >> 4
                else:
                    nib = bitmap_buf[byte_p] & 0x0F
                    byte_p += 1
                nibble_i += 1
                alpha = OPA4_TABLE[nib]
                dx = gx + col
                dy = gy + row
                if alpha > 0 and 0 <= dx < im.width and 0 <= dy < im.height:
                    bg = px[dx, dy]
                    px[dx, dy] = (
                        (fg[0] * alpha + bg[0] * (255 - alpha)) // 255,
                        (fg[1] * alpha + bg[1] * (255 - alpha)) // 255,
                        (fg[2] * alpha + bg[2] * (255 - alpha)) // 255,
                    )
        x += adv


def render_preview(groups: list[dict], glyph_dscs: list[dict], bitmap_buf: bytearray, out_png: Path):
    im = Image.new("RGB", (240, 280), (251, 248, 241))
    draw = ImageDraw.Draw(im)
    # Simulated quiz header, prompt box, and 4 choice buttons
    _draw_lvgl_line(im, groups, glyph_dscs, bitmap_buf, 22, 12, 140, "Japanese JLPT 1/5", (76, 29, 149))
    draw.rounded_rectangle([14, 34, 226, 108], radius=12, fill=(255, 255, 255), outline=(203, 213, 225))
    _draw_lvgl_line(
        im, groups, glyph_dscs, bitmap_buf, 20, 40, 198,
        "[N2] 傘(かさ)を忘(わす)れた日(ひ)___雨(あめ)が降(ふ)る。 (ironic 'just when')",
    )
    choices = ["につれて", "をもって", "に限(かぎ)って", "にかけて"]
    for i, ch_txt in enumerate(choices):
        cy = 112 + i * 37
        bg = (220, 252, 231) if i == 2 else (255, 255, 255)
        draw.rounded_rectangle([16, cy, 224, cy + 33], radius=10, fill=bg, outline=(203, 213, 225))
        _draw_lvgl_line(im, groups, glyph_dscs, bitmap_buf, 58, cy + 9, 196, ch_txt)

    out_png.parent.mkdir(parents=True, exist_ok=True)
    im.resize((480, 560), Image.Resampling.NEAREST).save(out_png)
    print(f"Saved LVGL raster preview -> {out_png}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", type=Path, default=None)
    args = ap.parse_args()
    groups, glyph_dscs, bitmap_buf = generate_c_font(OUT_C)
    if args.preview:
        render_preview(groups, glyph_dscs, bitmap_buf, args.preview)


if __name__ == "__main__":
    main()
