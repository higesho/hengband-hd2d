import tempfile,unittest
from pathlib import Path
from profiles import annotate

class MenuProfiles(unittest.TestCase):
    def compile(self,text,core='frox'):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'ability.c';path.write_text(text,encoding='ascii');audit=[]
            generated,_=annotate(path,core,menu_audit=audit)
            self.assertEqual(path.read_text(encoding='ascii'),text)
            return generated.decode('ascii'),audit
    def test_preview_returns_before_native_random_damage_preamble(self):
        code,audit=self.compile('void pulse(int cmd, variant *res) { int damage=randint1(100); switch(cmd) {case SPELL_CAST: fire_ball(GF_FIRE,0,damage,3); break;} }')
        self.assertTrue(audit[0]['cases'][0]['profile'])
        self.assertLess(code.index('if(cmd==HB_TARGET_PLAN_MODE)'),code.index('int damage=randint1'))
        self.assertEqual(code.count('randint1(100)'),1)
        prefix=code[:code.index('int damage=randint1')]
        self.assertIn('return;',prefix);self.assertNotIn('randint1',prefix)
    def test_unknown_preview_does_not_enter_original_preamble(self):
        code,audit=self.compile('void unknown(int cmd, var_ptr res) { mutate_player(); switch(cmd) {case SPELL_CAST: mysterious_shape();break;} }')
        self.assertIsNone(audit[0]['cases'][0]['profile'])
        self.assertLess(code.index('return;'),code.index('mutate_player()'))
        self.assertIn('hb_target_mark_plan',code)
    def test_directional_spell_is_not_replaced_by_fixed_preview(self):
        _,audit=self.compile('void aimed(int cmd,var_ptr res) {switch(cmd){case SPELL_CAST: {int dir;if(!get_aim_dir(&dir))return;fire_ball(GF_FIRE,dir,1,3);}break;}}')
        self.assertIsNone(audit[0]['cases'][0]['profile'])
    def test_level_dependent_aimed_and_self_effects_keep_separate_paths(self):
        code,audit=self.compile('void changing(int cmd,var_ptr res){switch(cmd){case SPELL_CAST:if(p_ptr->lev<30){int dir;if(!get_aim_dir(&dir))return;fire_ball(GF_FIRE,dir,1,2);}else{project_hack(GF_CHARM,p_ptr->lev);}break;}}')
        profile=audit[0]['cases'][0]['profile']
        self.assertIn('!(p_ptr->lev<30)',profile);self.assertIn('NATIVE_SHAPE_LOS',profile)
        self.assertNotIn('NATIVE_SHAPE_BALL',profile)
        self.assertIn('NATIVE_SHAPE_BALL',code)
    def test_local_radius_is_expanded_without_entering_cast_code(self):
        _,audit=self.compile('void burst(int cmd,var_ptr res) {int plev=p_ptr->lev;int radius=2+plev/10;switch(cmd){case SPELL_CAST:fire_ball(GF_FIRE,0,1,radius);break;}}')
        profile=audit[0]['cases'][0]['profile'];self.assertIn('p_ptr->lev',profile);self.assertNotIn('radius',profile);self.assertNotIn('plev',profile)
    def test_random_radius_does_not_leak_a_previously_rolled_value(self):
        _,audit=self.compile('void random_burst(int cmd,var_ptr res) {int radius=randint1(5);switch(cmd){case SPELL_CAST:fire_ball(GF_FIRE,0,1,radius);break;}}')
        self.assertIsNone(audit[0]['cases'][0]['profile'])
    def test_book_fallthrough_cases_keep_both_actions(self):
        code,audit=self.compile('static cptr do_book_spell(int spell,int mode){bool cast=mode==SPELL_CAST;switch(spell){case 1:case 2:if(cast){fire_ball(GF_FIRE,0,1,3);}break;}return "";}')
        cases=audit[0]['cases'];self.assertEqual([c['case'] for c in cases],['1','2']);self.assertTrue(all(c['profile'] for c in cases));self.assertIn('case 1:',code[:code.index('bool cast')]);self.assertIn('case 2:',code[:code.index('bool cast')])
    def test_multiple_actions_are_not_truncated_to_one(self):
        _,audit=self.compile('void several(int cmd,var_ptr res){switch(cmd){case SPELL_CAST:fire_ball(GF_FIRE,0,1,3);fire_ball(GF_COLD,0,1,5);break;}}')
        self.assertIsNone(audit[0]['cases'][0]['profile'])
    def test_book_dispatcher_bypasses_ordinary_global_write(self):
        code,audit=self.compile('cptr do_spell(int realm,int spell,int mode){cptr result=0;current=realm;switch(realm){case REALM_FIRE: result = do_fire_spell(spell, mode); break;}current=0;return result;}')
        self.assertLess(code.index('if(mode==HB_TARGET_PLAN_MODE)'),code.index('current=realm'))
        self.assertIn('return do_fire_spell(spell,mode)',code[:code.index('current=realm')])
        self.assertEqual(audit[0]['dispatcher_requires'],['do_fire_spell'])
    def test_heng_book_uses_private_enum_mode_before_preamble(self):
        code,audit=self.compile('tl::optional<std::string> do_book_spell(PlayerType *player_ptr,SPELL_IDX spell,SpellProcessType mode){int damage=randint1(100);bool cast=mode==SpellProcessType::CAST;switch(spell){case 1:if(cast){fire_ball(player_ptr,AttributeType::FIRE,Direction::self(),damage,3);}break;}return "";}','heng')
        self.assertTrue(audit[0]['cases'][0]['profile'])
        self.assertLess(code.index('if(static_cast<int>(mode)==HB_TARGET_PLAN_MODE)'),code.index('int damage=randint1'))
    def test_genso_book_keeps_native_dispatcher_out_of_preview_errors(self):
        code,audit=self.compile('cptr do_spell(int realm,int spell,int mode){switch(realm){case TV_BOOK_FIRE:return do_fire_spell(spell,mode);case TV_BOOK_MUSIC:if(p_ptr->pclass != CLASS_BARD) return do_new_spell_music2(spell,mode);else msg_print("error");default:msg_print("missing");}return NULL;}','genso')
        self.assertEqual(audit[0]['dispatcher_requires'],['do_fire_spell','do_new_spell_music2'])
        self.assertNotIn('msg_print',code[:code.index('return "";')])

    def test_genso_self_project_preserves_extra_native_argument(self):
        _,audit=self.compile('cptr pulse(int spell,int mode){bool cast=mode==SPELL_CAST;switch(spell){case 1:if(cast){project(0,3,py,px,100,GF_FIRE,PROJECT_GRID|PROJECT_KILL,-1);}break;}return "";}','genso')
        self.assertIn('NATIVE_SHAPE_BALL',audit[0]['cases'][0]['profile'])

    def test_genso_frozen_sight_area_does_not_evaluate_random_damage(self):
        code,audit=self.compile('cptr pulse(int spell,int mode){bool cast=mode==SPELL_CAST;switch(spell){case 1:if(cast){project_hack2(GF_KINKAKUJI,0,0,randint1(300));}break;}return "";}','genso')
        self.assertIn('NATIVE_SHAPE_LOS',audit[0]['cases'][0]['profile'])
        self.assertIn(',0,1,1)',audit[0]['cases'][0]['profile'])
        self.assertEqual(code.count('randint1'),1)
    def test_finished_preamble_branch_does_not_disable_the_spell_preview(self):
        _,audit=self.compile('cptr pulse(int spell,int mode){bool cast=mode==SPELL_CAST;if(unrelated_a){message();}else if(unrelated_b){message();}else{message();}switch(spell){case 1:if(cast){project_hack(GF_FIRE,100);}break;}return "";}','genso')
        profile=audit[0]['cases'][0]['profile'];self.assertIn('NATIVE_SHAPE_LOS',profile);self.assertNotIn('unrelated',profile)

if __name__=='__main__':unittest.main()
