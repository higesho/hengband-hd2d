"""攻撃の視覚効果の観測の呼び出しを、幻想蛮怒・Frox・Sil-Q・TOband の原作ソースへ足す。

2026-10-11（HUD を各コアへ広げる作業の一部）。記録するだけで、原作の変数・乱数・画面には触れない。
足す行はどれも「アダプタの関数を 1 回呼ぶ」だけで（`{extern ...; 関数(...);}` の 1 行）、原作の行は変えない・消さない。
場所は行ハッシュ（line_anchor.py）で決め、原作の文は quote しない。関数の中の 1 件だけに効くよう、探す範囲を関数の本体に絞る。

呼び先（アダプタ）:
  * 幻想蛮怒 gensoband/adapter/gb_shim.c の gb_fx_*
  * Frox     frox/adapter/fc_shim.c の fc_fx_*
  * Sil-Q    silq/adapter/sq_shim.c の sq_fx_*
  * TOband   toband/adapter/tb_shim.c の tb_fx_*（2026-10-17。幻想蛮怒と同じ書き足し）
積む先は presentation/bridge/named_attack_fx.h（属性を名前で積む記録の置き場）。

挿入の場所（幻想蛮怒・Frox の project() は同じ作りなので同じ行ハッシュ・同じ呼び出しで当たる）:
  project()      範囲（gx/gy/gm）が決まった直後、「爆発の無い場合を飛ばす」手前。経路・範囲・始点・終点・形の材料がそろう
  project()      GF_SEEKER / GF_SUPER_RAY は途中で戻るので、その分岐の手前で経路だけ記録する
  射撃           1 歩ごと（位置を保存した直後）と、当たったとき
  投擲           1 歩ごと・当たったとき・戻るとき（ブーメラン）
  project_hack2  幻想蛮怒だけ。project() を通らない「視界内の全員」の魔法（嵐のアンサンブル・金閣寺ほか）
"""
import re
import line_anchor as anchor


def _split(raw):
    text = raw.decode('latin1')
    newline = '\r\n' if '\r\n' in text else '\n'
    return text.replace('\r\n', '\n'), newline


def _join(text, newline):
    return text.replace('\n', newline).encode('latin1')


def _edit_function(text, header_regex, edit, what):
    """関数 1 つの本体（シグネチャの行から、行頭の '}' まで）に絞って edit(本体) を当てる。"""
    found = re.search(header_regex, text, re.M)
    if not found:
        raise ValueError(what + ': function not found')
    start = found.start()
    stop = text.index('\n}\n', start) + 3
    body = edit(text[start:stop])
    return text[:start] + body + text[stop:]


# ---------------------------------------------------------------------------------------------------------------------
# 行ハッシュ（原作の行の strip したものの SHA-256 先頭 12 桁。line_anchor.py）
# ---------------------------------------------------------------------------------------------------------------------
H = {
    # project()：爆発の無い場合を飛ばす手前のコメント行（幻想蛮怒は blazing_star() にも同じ行があるので project() に絞る）
    'speed_comment': '9e72c0af793b',
    'seeker': 'f999824eee4c',            # if( typ == GF_SEEKER )
    's_radiance': '7198a3df534f',        # Sil-Q do_cmd_fire()：止まった矢の判定のあと、光る矢の radiance を足す行
    # 幻想蛮怒 do_cmd_fire_aux()
    'g_save_loc': '454f1abe7c8b',        # /* Save the new location */
    'g_x_nx': '8aeffa7fe190',            # x = nx;
    'g_y_ny': '6a85a9f9f41e',            # y = ny;
    'g_fire_hit': 'fe94c6d8450b',        # if (test_hit_fire(chance - cur_dis, armour, m_ptr->ml))
    'fear_decl': 'cf011f8758ca',         # bool fear = FALSE;
    # 幻想蛮怒 do_cmd_throw_aux()
    'g_advance_comment': '0424c90cb0a2',  # /* Advance the distance */
    'g_cur_dis_inc': '5d805dd5ecc7',     # cur_dis++;
    'g_throw_hit': 'e16b46ad727c',       # if (test_hit_fire(chance - cur_dis, mon_ac, m_ptr->ml))
    'g_return_loop': 'c628cce7be60',     # for (i = cur_dis - 1; i > 0; i--)
    # Frox
    'f_if_hit': '98c4d2363512',          # if (hit)
    'f_animate': '7066ce7fa0e0',         # _animate(context);
    'f_return_loop': 'cd4f34211b46',     # for (; context->path_pos >= 0; context->path_pos--)
    # Sil-Q
    's_clear_temp': 'bbf7a14d2cb1',      # /* Clear the "temp" array  XXX */
    's_if_hit_comment': '98958bcd84d4',  # /* If it hit */
    's_if_hit_comment_throw': '768207ec5650',  # /* If it hit... */
    's_advance': 'f34463d69325',         # /* Advance */
    # TOband（変愚蛮怒 1.x 系の平屋の C。幻想蛮怒に近い作り）
    't_fire_hit': '13de0023700e',        # if (test_hit_fire(chance - cur_dis, ac, m_ptr->ml))（射撃と投擲に 1 つずつ）
    't_return_loop': '86fb73b1f368',     # for (i = cur_dis-1;i>0;i--)（投擲の戻り）
}


def _h(line):
    return anchor.line_hash(line)


# ---------------------------------------------------------------------------------------------------------------------
# 幻想蛮怒・Frox 共通（project() は同じ作り）
# ---------------------------------------------------------------------------------------------------------------------
def _project_hook(prefix):
    return ('\t{extern void %s_fx_note_projection(int,int,int,int,int,int,int,int,int,int,int,const u16b *,int,const byte *,'
            'const byte *,int);%s_fx_note_projection(who,y1,x1,by,bx,typ,rad,flg,blind,breath,jump,path_g,path_n,gy,gx,grids);}') % (prefix, prefix)


def _ray_hook(prefix):
    return ('\tif(typ==GF_SEEKER||typ==GF_SUPER_RAY){extern void %s_fx_note_ray(int,int,int,int,int,int,int,const u16b *,int);'
            '%s_fx_note_ray(who,y1,x1,typ,flg,blind,jump,path_g,path_n);}') % (prefix, prefix)


def _legacy_project(raw, prefix, header_regex):
    text, newline = _split(raw)

    def edit(body):
        body = anchor.insert_before(body, H['speed_comment'], _project_hook(prefix), prefix + ' attack effect projection site')
        return anchor.insert_before(body, H['seeker'], _ray_hook(prefix), prefix + ' attack effect ray site')

    return _join(_edit_function(text, header_regex, edit, prefix + ' project()'), newline)


def genso_project(raw):
    """幻想蛮怒の spells1.c の project()。"""
    return _legacy_project(raw, 'gb', r'^bool project\(int who, int rad, int y, int x, int dam, int typ, int flg, int monspell\)')


def frox_project(raw):
    """Frox の spells1.c の project()。"""
    return _legacy_project(raw, 'fc', r'^bool project\(int who, int rad, int y, int x, int dam, int typ, int flg\)')


def genso_hack2(raw):
    """幻想蛮怒 spells2.c の project_hack2()（視界内の全員に別々の属性をかける魔法。project() を通らない）。
    視界内の判定の配列 flag_in_sight が出そろった所（画面の効果の手前）へ 1 行。"""
    text, newline = _split(raw)
    hook = ('\t{extern void gb_fx_note_all_sight(int,bool (*)[MAX_WID]);gb_fx_note_all_sight(typ_base,flag_in_sight);}')

    def edit(body):
        # 「画面効果」のコメント行の手前。コメント行は日本語（CP932）なのでハッシュで探す。
        return anchor.insert_before(body, H_HACK2_COMMENT, hook, 'genso project_hack2 site')

    return _join(_edit_function(text, r'^bool project_hack2\(int typ_base, int dice, int sides, int base\)', edit,
                                'genso project_hack2'), newline)


# project_hack2 の「画面効果」のコメント行（/*:::画面効果*/）のハッシュ。原作の行そのものは書かない。
H_HACK2_COMMENT = 'e9324593ad06'


def genso_cmd2(raw):
    """幻想蛮怒 cmd2.c：射撃（do_cmd_fire_aux）と投擲（do_cmd_throw_aux）。"""
    text, newline = _split(raw)

    def fire(body):
        body = anchor.insert_after(
            body, [H['g_save_loc'], H['g_x_nx'], H['g_y_ny']],
            '\t\t{extern void gb_fx_shot(int,int,int,int,int,const object_type *,const object_type *);'
            'gb_fx_shot(cur_dis,y,x,prev_y,prev_x,q_ptr,j_ptr);}', 'genso fire step site')
        return anchor.insert_after(body, [H['g_fire_hit'], _h('{'), H['fear_decl']],
                                   '\t\t\t\t{extern void gb_fx_hit(void);gb_fx_hit();}', 'genso fire hit site')

    def throw(body):
        body = anchor.insert_after(
            body, [H['g_advance_comment'], H['g_cur_dis_inc']],
            '\t\t{extern void gb_fx_throw(int,int,int,int,int,const object_type *);gb_fx_throw(cur_dis,y,x,prev_y,prev_x,q_ptr);}',
            'genso throw step site')
        body = anchor.insert_after(body, [H['g_throw_hit'], _h('{'), H['fear_decl']],
                                   '\t\t\t\t{extern void gb_fx_hit(void);gb_fx_hit();}', 'genso throw hit site')
        return anchor.insert_before(body, H['g_return_loop'],
                                    '\t\t\t{extern void gb_fx_throw_return(void);gb_fx_throw_return();}', 'genso throw return site')

    text = _edit_function(text, r'^void do_cmd_fire_aux\(int item, object_type \*j_ptr\)', fire, 'genso do_cmd_fire_aux')
    text = _edit_function(text, r'^bool do_cmd_throw_aux\(int mult, bool boomerang, int shuriken\)', throw, 'genso do_cmd_throw_aux')
    return _join(text, newline)


def frox_cmd2(raw):
    """Frox cmd2.c：射撃（do_cmd_fire_aux2）。"""
    text, newline = _split(raw)

    def fire(body):
        body = anchor.insert_after(
            body, [H['g_save_loc'], H['g_x_nx'], H['g_y_ny']],
            '            {extern void fc_fx_shot(int,int,int,int,int,const object_type *,const object_type *);'
            'fc_fx_shot(cur_dis,y,x,prev_y,prev_x,&arrow,bow);}', 'frox fire step site')
        return anchor.insert_after(body, [H['f_if_hit'], _h('{'), H['fear_decl']],
                                   '                    {extern void fc_fx_hit(void);fc_fx_hit();}', 'frox fire hit site')

    return _join(_edit_function(text, r'^void do_cmd_fire_aux2\(obj_ptr bow, obj_ptr arrows, int sx, int sy, int tx, int ty\)',
                                fire, 'frox do_cmd_fire_aux2'), newline)


def frox_throw(raw):
    """Frox py_throw.c：投擲（_throw の 1 歩・_hit_mon の命中・_return の戻り）。"""
    text, newline = _split(raw)

    def step(body):
        return anchor.insert_after(
            body, H['f_animate'],
            '        {extern void fc_fx_throw(int,const u16b *,const object_type *);fc_fx_throw(context->path_pos,context->path,context->obj);}',
            'frox throw step site')

    def hit(body):
        return anchor.insert_after(body, H['fear_decl'], '        {extern void fc_fx_hit(void);fc_fx_hit();}', 'frox throw hit site')

    def back(body):
        return anchor.insert_before(body, H['f_return_loop'],
                                    '        {extern void fc_fx_throw_return(void);fc_fx_throw_return();}', 'frox throw return site')

    text = _edit_function(text, r'^void _throw\(py_throw_ptr context\)', step, 'frox _throw')
    text = _edit_function(text, r'^bool _hit_mon\(py_throw_ptr context, int m_idx\)', hit, 'frox _hit_mon')
    text = _edit_function(text, r'^void _return\(py_throw_ptr context\)', back, 'frox _return')
    return _join(text, newline)


# ---------------------------------------------------------------------------------------------------------------------
# Sil-Q
# ---------------------------------------------------------------------------------------------------------------------
def silq_project(raw):
    """Sil-Q の spells1.c の project()。範囲（gy/gx/gd）が決まった直後、一時配列を片づける手前。"""
    text, newline = _split(raw)
    # 1 行目で渡す材料: who, rad, y0, x0, y1, x1, y2, x2, flg, typ, blind, path_g, path_n, gy, gx, gd。grids は 2 行目（件数）。
    hook = ('    {extern void sq_fx_note_projection(int,int,int,int,int,int,int,int,u32b,int,int,const u16b *,int,int,'
            'const byte *,const byte *,const byte *);sq_fx_note_projection(who,rad,y0,x0,y1,x1,y2,x2,flg,typ,blind,path_g,path_n,grids,'
            'gy,gx,gd);}')

    def edit(body):
        return anchor.insert_before(body, H['s_clear_temp'], hook, 'silq attack effect projection site')

    return _join(_edit_function(text, r'^bool project\(int who, int rad, int y0, int x0, int y1, int x1, int dd, int ds,', edit,
                                'silq project()'), newline)


def silq_cmd2(raw):
    """Sil-Q の cmd2.c：射撃（do_cmd_fire）と投擲（do_cmd_throw）。"""
    text, newline = _split(raw)

    def fire(body):
        # 矢が止まったあと（ghost_arrow）の歩を記録しないよう、止まった矢の判定を抜けたあとの radiance の行の次に置く。
        body = anchor.insert_after(
            body, H['s_radiance'],
            '            {extern void sq_fx_shot(int,int,int,const object_type *,const object_type *);sq_fx_shot(i,y,x,i_ptr,j_ptr);}',
            'silq fire step site')
        return anchor.insert_before(body, H['s_if_hit_comment'],
                                    '                if (hit_result > 0) {extern void sq_fx_hit(void);sq_fx_hit();}', 'silq fire hit site')

    def throw(body):
        body = anchor.insert_after(
            body, [H['s_advance'], H['g_x_nx'], H['g_y_ny']],
            '        {extern void sq_fx_throw(int,int,int,const object_type *);sq_fx_throw(i,y,x,i_ptr);}', 'silq throw step site')
        return anchor.insert_before(body, H['s_if_hit_comment_throw'],
                                    '            if (hit_result > 0) {extern void sq_fx_hit(void);sq_fx_hit();}', 'silq throw hit site')

    text = _edit_function(text, r'^void do_cmd_fire\(int quiver\)', fire, 'silq do_cmd_fire')
    text = _edit_function(text, r'^void do_cmd_throw\(bool automatic\)', throw, 'silq do_cmd_throw')
    return _join(text, newline)


# ---------------------------------------------------------------------------------------------------------------------
# TOband（2026-10-17）
# ---------------------------------------------------------------------------------------------------------------------
def toband_project(raw):
    """TOband の spells1.c の project()。範囲（gx/gy）が決まった直後、「爆発の無い場合を飛ばす」手前に 1 行。
    TOband には GF_SEEKER・GF_SUPER_RAY が無く、project() の途中で戻る分岐も無いので、挿入はここ 1 か所だけ。
    着弾点は「爆発の中心を保存」した y2・x2、始点は y1・x1。"""
    text, newline = _split(raw)
    hook = ('\t{extern void tb_fx_note_projection(int,int,int,int,int,int,int,int,int,int,int,const u16b *,int,const byte *,'
            'const byte *,int);tb_fx_note_projection(who,y1,x1,y2,x2,typ,rad,(int)flg,blind,breath,jump,path_g,path_n,gy,gx,grids);}')

    def edit(body):
        return anchor.insert_before(body, H['speed_comment'], hook, 'toband attack effect projection site')

    return _join(_edit_function(text, r'^bool project\(int who, int rad, int y, int x, int dam, int typ, u32b flg, int mod_elem_mode\)',
                                edit, 'toband project()'), newline)


def toband_cmd2(raw):
    """TOband の cmd2.c：射撃（do_cmd_fire_aux）と投擲（do_cmd_throw_aux）。
    1 歩ごとは「新しい位置を保存する」手前に置く（その時点では y・x が前の位置、新しい位置は射撃が ny・nx、投擲が ny[cur_dis-1]・nx[cur_dis-1]）。"""
    text, newline = _split(raw)

    def fire(body):
        body = anchor.insert_before(
            body, H['g_save_loc'],
            '\t\t\t{extern void tb_fx_shot(int,int,int,int,int,const object_type *,const object_type *,int);'
            'tb_fx_shot(cur_dis,ny,nx,y,x,q_ptr,j_ptr,shot_flgs);}', 'toband fire step site')
        return anchor.insert_after(body, [H['t_fire_hit'], _h('{'), H['fear_decl']],
                                   '\t\t\t\t\t{extern void tb_fx_hit(void);tb_fx_hit();}', 'toband fire hit site')

    def throw(body):
        body = anchor.insert_before(
            body, H['g_save_loc'],
            '\t\t{extern void tb_fx_throw(int,int,int,int,int,const object_type *);'
            'tb_fx_throw(cur_dis,ny[cur_dis-1],nx[cur_dis-1],y,x,q_ptr);}', 'toband throw step site')
        body = anchor.insert_after(body, [H['t_fire_hit'], _h('{'), H['fear_decl']],
                                   '\t\t\t\t{extern void tb_fx_hit(void);tb_fx_hit();}', 'toband throw hit site')
        return anchor.insert_before(body, H['t_return_loop'],
                                    '\t\t\t\t{extern void tb_fx_throw_return(void);tb_fx_throw_return();}', 'toband throw return site')

    text = _edit_function(text, r'^bool do_cmd_fire_aux\(int item, object_type \*j_ptr, int shot_flgs, int x_to_h, int x_to_d, int penetrate, bool direct\)',
                          fire, 'toband do_cmd_fire_aux')
    text = _edit_function(text, r'^bool do_cmd_throw_aux\(int mult, u16b mode, int chosen_item\)', throw, 'toband do_cmd_throw_aux')
    return _join(text, newline)
