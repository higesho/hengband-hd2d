"""TOband の経路・視線・分解の範囲・ブレスの形を、読み取りの述語に差し替えて写す（幻想蛮怒の genso_geometry.py に相当）。

TOband（変愚蛮怒 1.5.1 系）の project_path()・los()・in_disintegration_range()・breath_shape() の本体はそのまま写し、
地形とモンスターを調べる 3 つのマクロだけを、プレイヤが知っている地形・見えているモンスターで答える述語に替える。

  cave_floor_bold(Y,X)             → _tb_preview_floor(Y,X)         （toband/adapter/tb_targeting_geometry.inl）
  stoppable_bold(Y,X,FLG)          → _tb_preview_stoppable(Y,X,FLG) （原作のマクロの本体から作る）
  cave_stop_disintegration(Y,X)    → _tb_preview_stop_disintegration(Y,X)（同上）

後ろ 2 つは原作のマクロの定義（spells1.c の #define）を読んで関数にする。定義が変わったら作り直すので、
原作の判定とずれない。breath_shape() が実際にしか通らない do_disintegration()（地形を壊す）は、
プレビューでは通らない枝だが、地形を壊す関数を残さないため in_disintegration_range() に替える。
"""
import re


def _macro(text, name):
    """`#define name(...) \\ ...` の本体（行末の \\ をつなげたもの）を返す。引数の並びも返す。"""
    found = re.search(r'^[ \t]*#define[ \t]+' + re.escape(name) + r'\(([^)]*)\)[ \t]*((?:[^\n]*\\\n)*[^\n]*)', text, re.M)
    if not found:
        raise ValueError('TOband macro missing: ' + name)
    args = [a.strip() for a in found.group(1).split(',')]
    body = re.sub(r'\\\n', ' ', found.group(2)).strip()
    return args, body


def generate(spells, cave):
    spells_text = spells.decode('latin1').replace('\r\n', '\n')
    cave_text = cave.decode('latin1').replace('\r\n', '\n')

    def function(text, signature):
        start = text.index(signature)
        stop = text.index('\n}', start) + 2
        return text[start:stop]

    path = function(spells_text, 'sint project_path(').replace('sint project_path(', 'static sint _tb_preview_project_path(', 1)
    los = function(cave_text, 'bool los(').replace('bool los(', 'static bool _tb_preview_los(', 1)
    disi = function(spells_text, 'bool in_disintegration_range(').replace(
        'bool in_disintegration_range(', 'static bool _tb_preview_disintegration_range(', 1)
    breath = function(spells_text, 'void breath_shape(').replace('void breath_shape(', 'static void _tb_preview_breath_shape(', 1)
    text = '\n'.join((path, los, disi, breath))
    text = re.sub(r'\bcave_floor_bold\(', '_tb_preview_floor(', text)
    text = re.sub(r'\bcave_stop_disintegration\(', '_tb_preview_stop_disintegration(', text)
    text = re.sub(r'\bstoppable_bold\(', '_tb_preview_stoppable(', text)
    text = re.sub(r'\bdo_disintegration\(', '_tb_preview_disintegration_range(', text)
    text = re.sub(r'\blos\(', '_tb_preview_los(', text)
    text = re.sub(r'\bin_disintegration_range\(', '_tb_preview_disintegration_range(', text)
    if re.search(r'\bcave\[|\brand\w*\(|\bprojectable\(', text):
        raise ValueError('Unreviewed mutable geometry dependency')
    # 原作のマクロから、知っているマスだけで答える関数を作る。
    stop_args, stop_body = _macro(spells_text, 'cave_stop_disintegration')
    stop_body = re.sub(r'cave\[' + stop_args[0] + r'\]\[' + stop_args[1] + r'\]\.feat', 'feat', stop_body)
    if 'cave[' in stop_body:
        raise ValueError('cave_stop_disintegration uses more than the feature')
    stoppable_args, stoppable_body = _macro(spells_text, 'stoppable_bold')
    y, x, flg = stoppable_args
    stoppable_body = stoppable_body.replace('cave[(' + y + ')][(' + x + ')].m_idx', '_tb_preview_monster((' + y + '),(' + x + '))')
    if re.search(r'cave\[[^\]]*\]\[[^\]]*\]\.m_idx', stoppable_body):
        raise ValueError('stoppable_bold reads the real creature index')
    predicates = (
        'static int _tb_preview_stop_disintegration(int %s,int %s)\n{\n    int feat=_tb_preview_feat(%s,%s);\n    return %s;\n}\n'
        % (stop_args[0], stop_args[1], stop_args[0], stop_args[1], stop_body) +
        'static int _tb_preview_stoppable(int %s,int %s,u32b %s)\n{\n    return %s;\n}\n' % (y, x, flg, stoppable_body))
    notices = []
    for raw in (spells, cave):
        for comment in re.findall(r'/\*.*?\*/', raw.decode('latin1'), re.S):
            if 'copyright' in comment.lower() and comment not in notices:
                notices.append(comment)
    return ('\n'.join(notices) + '\n/* Source-derived native geometry; no hidden grid or creature queries. */\n'
            + predicates + text + '\n').encode('latin1')
