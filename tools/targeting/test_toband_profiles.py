"""TOband（変愚蛮怒 1.5.1 系）の狙いの計装の単体テスト（幻想蛮怒の test_profiles.py・test_menu_profiles.py・test_item_profiles.py に相当）。

原作の関数の形（引数の数・ローカル変数）は TOband のものに合わせてある。原作を書き換えないこと・乱数を引かないことは
ほかのテストと同じ作りで確かめる。"""
import tempfile,unittest
from pathlib import Path
from profiles import annotate,ELEMENTS
from projection_helpers import ProjectionHelpers
from profile_inventory import Source
from self_profiles import scatter_plan

class ToBandProfiles(unittest.TestCase):
    def derive(self,name,body,helpers=None,audit=None,core='toband'):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/name;path.write_text(body,encoding='ascii')
            after,rows=annotate(path,core,helpers=helpers,menu_audit=audit)
            self.assertEqual(path.read_text(encoding='ascii'),body)
            return after.decode('ascii'),rows
    def apply(self,body,name='spells_a.c',core='toband'):
        return self.derive(name,'void action(void) { int dir; '+body+' }\n',core=core)

    def test_ball_takes_the_no_reduce_argument_without_changing_the_shape(self):
        after,rows=self.apply('if (!get_aim_dir(&dir)) return; fire_ball(GF_FIRE,dir,damroll(4,6),2,FALSE);')
        self.assertTrue(rows[0]['supported']);self.assertIn('NATIVE_SHAPE_BALL,GF_FIRE,2',rows[0]['profile'])
        self.assertNotIn('FALSE',rows[0]['profile']);self.assertNotIn('damroll',rows[0]['profile'])
        self.assertEqual(after.count('damroll(4,6)'),1)
    def test_negative_radius_is_a_breath_and_hidden_ball_keeps_its_flag(self):
        _,rows=self.apply('if (!get_aim_dir(&dir)) return; fire_ball(GF_PURE_FIRE,dir,240,-3,FALSE);')
        self.assertTrue(rows[0]['supported']);self.assertIn('NATIVE_SHAPE_BREATH,GF_PURE_FIRE,-3',rows[0]['profile'])
        _,rows=self.apply('if (!get_aim_dir(&dir)) return; fire_ball_hide(GF_GENOCIDE,dir,250,0,FALSE);')
        self.assertTrue(rows[0]['supported']);self.assertIn('PROJECT_HIDE',rows[0]['profile'])
    def test_rocket_keeps_its_own_shape(self):
        _,rows=self.apply('if (!get_aim_dir(&dir)) return; fire_rocket(GF_ROCKET,dir,100,2,FALSE);')
        self.assertTrue(rows[0]['supported']);self.assertIn('NATIVE_SHAPE_ROCKET',rows[0]['profile'])
    def test_blast_uses_six_arguments_and_reflectable_flag(self):
        after,rows=self.apply('if (!get_aim_dir(&dir)) return; fire_blast(GF_LITE,dir,damroll(3,6),2,10,3);')
        self.assertTrue(rows[0]['supported']);self.assertIn('NATIVE_SHAPE_BLAST',rows[0]['profile']);self.assertIn('PROJECT_REFLECTABLE',rows[0]['profile'])
        self.assertNotIn('damroll',rows[0]['profile']);self.assertEqual(after.count('damroll'),1)
    def test_toband_only_attributes_are_reviewed_for_toband_and_not_leaked_to_other_cores(self):
        _,rows=self.apply('if (!get_aim_dir(&dir)) return; fire_bolt(GF_PURE_AQUA,dir,1);')
        self.assertTrue(rows[0]['supported'])
        self.assertIn('PURE_AQUA',__import__('profiles').TOBAND_ELEMENTS)
        # 続けて別のコアを処理すると、TOband の表は見ない（ほかのコアの出力を変えない）
        _,rows=self.apply('if (!get_aim_dir(&dir)) return; fire_bolt(GF_PURE_AQUA,dir,1);',core='genso')
        self.assertFalse(rows[0]['supported'])
        self.assertNotIn('PURE_AQUA',ELEMENTS)
    def test_charge_through_attack_is_not_given_a_shape(self):
        _,rows=self.apply('if (!get_aim_dir(&dir)) return; fire_bolt(GF_ATTACK,dir,1);')
        self.assertFalse(rows[0]['supported'])
    def test_raw_project_accepts_the_elem_mode_but_not_a_surprising_last_argument(self):
        body='int ty,tx;if(!get_aim_dir(&dir))return FALSE;tx=px+99*ddx[dir];ty=py+99*ddy[dir];if((dir==5)&&target_okay()){tx=target_col;ty=target_row;}'
        _,rows=self.apply(body+'project(0,2,ty,tx,1,GF_MISSILE,PROJECT_STOP|PROJECT_KILL,MODIFY_ELEM_MODE_MAGIC);')
        self.assertTrue(rows[0]['supported']);self.assertIn('PROJECT_STOP|PROJECT_KILL',rows[0]['profile'])
        _,rows=self.apply(body+'project(0,2,ty,tx,1,GF_MISSILE,PROJECT_STOP|PROJECT_KILL,-1);')
        self.assertFalse(rows[0]['supported'])
    def test_adjacent_melee_is_a_melee_shape(self):
        _,rows=self.apply('int y,x;if(!get_rep_dir(&dir,FALSE))return;y=py+ddy[dir];x=px+ddx[dir];py_attack(y,x,0);')
        self.assertTrue(rows[0]['supported']);self.assertIn('NATIVE_SHAPE_MELEE',rows[0]['profile'])

    def test_fire_profile_reads_only_locals_and_the_launcher_range(self):
        body=('bool do_cmd_fire_aux(int item,object_type *j_ptr,int shot_flgs,int x_to_h,int x_to_d,int penetrate,bool direct)'
              '{int dir;bool as_beam=FALSE;bool dcfa_thru=FALSE;project_length=11;if(!get_aim_dir(&dir))return FALSE;damroll(1,2);return TRUE;}')
        after,rows=self.derive('cmd2.c',body)
        self.assertTrue(rows[0]['supported']);profile=rows[0]['profile']
        self.assertIn('NATIVE_SHAPE_SHOT',profile);self.assertIn('project_length',profile);self.assertIn('penetrate>0',profile)
        self.assertNotIn('damroll',profile);self.assertEqual(after.count('damroll(1,2)'),1)
    def test_throw_profile_skips_the_shape_for_a_shooting_star(self):
        body=('bool do_cmd_throw_aux(int mult,u16b mode,int chosen_item){int dir;bool shooting_star=(mode&1)?TRUE:FALSE;'
              'project_length=9;if(!get_aim_dir(&dir))return FALSE;return TRUE;}')
        _,rows=self.derive('cmd2.c',body)
        self.assertTrue(rows[0]['supported']);profile=rows[0]['profile']
        self.assertIn('NATIVE_SHAPE_THROW',profile);self.assertIn('shooting_star?',profile);self.assertIn('hb_target_profile_known(0)',profile)

    def test_wand_table_is_read_from_the_effect_switch_and_never_replays_damage(self):
        body=('static int wand_effect(int sval,int dir){int ident=FALSE;switch(sval){'
              'case SV_WAND_FIRE_BALL:if(fire_ball(GF_FIRE,dir,70+3*p_ptr->lev/4,2,FALSE))ident=TRUE;break;'
              'case SV_WAND_LITE:lite_line(dir);ident=TRUE;break;}return ident;}'
              'static void do_cmd_aim_wand_aux(int item){object_type *o_ptr=&inventory[item];int dir;if(!get_aim_dir(&dir))return;wand_effect(o_ptr->sval,dir);}')
        helpers=ProjectionHelpers()
        with tempfile.TemporaryDirectory() as directory:
            catalog=Path(directory)/'spells2.c'
            catalog.write_text('bool lite_line(int dir){u32b flg=PROJECT_BEAM|PROJECT_GRID|PROJECT_KILL;return project_hook(GF_LITE_WEAK,dir,damroll(6,8),flg);}',encoding='ascii')
            helpers.read(catalog)
        audit=[]
        code,rows=self.derive('cmd6.c',body,helpers,audit)
        plan=code[code.index('static void hb_target_toband_wand_profile'):]
        self.assertIn('SV_WAND_FIRE_BALL',plan);self.assertIn('NATIVE_SHAPE_BALL,GF_FIRE,2',plan);self.assertIn('NATIVE_SHAPE_BEAM,GF_LITE_WEAK',plan)
        self.assertNotIn('p_ptr->lev/4',plan)
        self.assertTrue(rows[0]['supported']);self.assertIn('hb_target_toband_wand_profile(o_ptr->sval,0,0)',rows[0]['profile'])
        self.assertIn('object_is_aware(o_ptr)',rows[0]['profile'])
    def test_dragon_breath_wand_lists_every_possible_element(self):
        body=('static int wand_effect(int sval,int dir){int ident=FALSE;switch(sval){case SV_WAND_DRAGON_BREATH:{switch(randint1(5)){'
              'case 1:{fire_ball(GF_PURE_EARTH,dir,240,-3,FALSE);break;}case 2:{fire_ball(GF_PURE_WIND,dir,210,-3,FALSE);break;}'
              'case 3:{fire_ball(GF_PURE_FIRE,dir,240,-3,FALSE);break;}case 4:{fire_ball(GF_PURE_AQUA,dir,210,-3,FALSE);break;}'
              'default:{fire_ball(GF_POIS,dir,180,-3,FALSE);break;}}ident=TRUE;break;}}return ident;}')
        code,_=self.derive('cmd6.c',body,ProjectionHelpers(),[])
        plan=code[code.index('static void hb_target_toband_wand_profile'):]
        for element in ('PURE_EARTH','PURE_WIND','PURE_FIRE','PURE_AQUA','POIS'):self.assertIn('NATIVE_SHAPE_BREATH,GF_'+element,plan)
        self.assertEqual(plan.count('NATIVE_SHAPE_BREATH'),5)
        self.assertNotIn('randint1',plan)
    def test_staff_starlight_uses_the_line_of_sight_scatter(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'cmd6.c'
            path.write_text('int staff_effect(int sval,bool *use_charge,bool known){int k;switch(sval){case SV_STAFF_STARLITE:{int num=damroll(5,3);int y,x;int attempts;'
                'for(k=0;k<num;k++){attempts=1000;while(attempts--){scatter(&y,&x,py,px,4,0);if(!cave_floor_bold(y,x))continue;if((y!=py)||(x!=px))break;}'
                'project(0,0,y,x,damroll(6+p_ptr->lev/8,10),GF_LITE_WEAK,(PROJECT_BEAM|PROJECT_THRU|PROJECT_GRID|PROJECT_KILL),MODIFY_ELEM_MODE_MAGIC);}break;}}return 1;}',encoding='ascii')
            source=Source(path);owner=next(f for f in source.functions if f[2]=='staff_effect')
            plan=scatter_plan(source,owner[0],owner[1],owner,'toband')
            self.assertEqual(plan,['GF_LITE_WEAK','4','(PROJECT_BEAM|PROJECT_THRU|PROJECT_GRID|PROJECT_KILL)','1'])
            self.assertIsNone(scatter_plan(source,owner[0],owner[1],owner,'frox'))

    def test_book_spell_reads_the_caster_skill_through_the_reviewed_accessor(self):
        body=('static cptr do_magery_spell(int spell,int mode){bool name=(mode==SPELL_NAME)?TRUE:FALSE;bool cast=(mode==SPELL_CAST)?TRUE:FALSE;'
              'int mlev=party_spell_caster()->magic_exp[REALM_MAGERY]/10;switch(spell){case 4:if(cast){int dice=2;int sides=mlev/2;int rad=mlev/10+1;lite_area(damroll(dice,sides),rad);}break;}return "";}'
              'static cptr do_spell_aux(int realm,int spell,int mode){switch(realm){case REALM_MAGERY: return do_magery_spell(spell, mode);}return NULL;}'
              'cptr do_spell(int realm,int spell,int mode){int caster=-1;cptr result;if(mode==SPELL_CAST)caster=party_spell_target_begin();result=do_spell_aux(realm,spell,mode);party_spell_target_end(caster);return result;}')
        audit=[]
        code,_=self.derive('do-spell.c',body,ProjectionHelpers(),audit)
        book=next(e for e in audit if e.get('book'));self.assertEqual(book['function'],'do_magery_spell')
        profile=book['cases'][0]['profile'];self.assertIn('party_spell_caster()->magic_exp[REALM_MAGERY]/10',profile)
        self.assertIn('NATIVE_SHAPE_BALL',profile);self.assertIn('NATIVE_SHAPE_ROOM_AREA',profile);self.assertNotIn('damroll',profile)
        self.assertLess(code.index('if(mode==HB_TARGET_PLAN_MODE)'),code.index('int mlev=party_spell_caster'))
        dispatcher=next(e for e in audit if e.get('dispatcher_requires'))
        self.assertEqual(dispatcher['dispatcher_requires'],['do_magery_spell']);self.assertEqual(dispatcher['function'],'do_spell')
        # 合図は外向きの do_spell の記号で出す（メニュー側が見る記号）。振り分けの static な do_spell_aux の頭に足す。
        aux=code[code.index('static cptr do_spell_aux'):code.index('cptr do_spell(int realm')]
        self.assertIn('hb_target_mark_plan((native_target_plan_symbol)do_spell)',aux)
        self.assertEqual(code.count('party_spell_target_begin()'),1)
    def test_other_caster_fields_are_not_assumed_pure(self):
        body='static cptr do_magery_spell(int spell,int mode){bool cast=mode==SPELL_CAST;int mlev=party_spell_caster()->csp/10;switch(spell){case 4:if(cast){lite_area(damroll(2,3),mlev);}break;}return "";}'
        audit=[];self.derive('do-spell.c',body,ProjectionHelpers(),audit)
        self.assertIsNone(next(e for e in audit if e.get('book'))['cases'][0]['profile'])
    def test_destruction_and_quake_use_toband_arities(self):
        body=('static cptr do_earth_spell(int spell,int mode){bool cast=mode==SPELL_CAST;switch(spell){case 1:if(cast){earthquake(py,px,10);}break;'
              'case 2:if(cast){destroy_area(py,px,13+randint0(5));}break;}return "";}')
        audit=[];code,_=self.derive('do-spell.c',body,ProjectionHelpers(),audit)
        entries={c['case']:c['profile'] for c in next(e for e in audit if e.get('book'))['cases']}
        self.assertIn('NATIVE_SHAPE_QUAKE',entries['1']);self.assertIn('NATIVE_SHAPE_DESTRUCTION',entries['2'])
        self.assertEqual(code.count('randint0'),1)

if __name__=='__main__':unittest.main()
