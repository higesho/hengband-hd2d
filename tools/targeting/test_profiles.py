import tempfile,unittest
from pathlib import Path
from profiles import annotate
from projection_helpers import ProjectionHelpers

class Profiles(unittest.TestCase):
    def apply(self,body,name='spells_a.c',prefix='',core='frox'):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/name
            before=(prefix+'void action(void) { int dir; '+body+' }\r\n').encode('ascii')
            path.write_bytes(before);after,rows=annotate(path,core)
            self.assertEqual(path.read_bytes(),before)
            return after.decode('ascii'),rows
    def test_damage_rng_stays_only_in_original_action(self):
        after,rows=self.apply('if (!get_fire_dir(&dir)) return; fire_bolt(GF_FIRE,dir,damroll(4,6));')
        self.assertTrue(rows[0]['supported']);self.assertEqual(after.count('damroll(4,6)'),1)
        self.assertIn('fire_bolt(GF_FIRE,dir,damroll(4,6))',after)
    def test_successful_input_body_has_its_own_forecast(self):
        after,rows=self.apply('if(get_fire_dir(&dir)){fire_ball(GF_FIRE,dir,damroll(4,6),-2);}')
        self.assertTrue(rows[0]['supported']);self.assertIn('NATIVE_SHAPE_BREATH',rows[0]['profile'])
        self.assertEqual(after.count('damroll(4,6)'),1)
    def test_second_input_is_not_assumed_to_have_succeeded(self):
        _,rows=self.apply('if(get_fire_dir(&dir)){if(get_fire_dir(&dir)){fire_bolt(GF_FIRE,dir,1);}}')
        self.assertFalse(rows[0]['supported']);self.assertTrue(rows[1]['supported'])
    def test_raw_adjacent_jump_preserves_native_flags_and_origin(self):
        _,rows=self.apply('int y,x;int rad=1+p_ptr->lev/24;if(!get_rep_dir2(&dir))return NULL;if(dir==5)return NULL;y=py+ddy[dir];x=px+ddx[dir];project(0,rad,y,x,damroll(5,6),GF_DISINTEGRATE,PROJECT_JUMP|PROJECT_KILL|PROJECT_GRID,-1);',core='genso')
        self.assertTrue(rows[0]['supported']);self.assertIn('NATIVE_SHAPE_PROJECT',rows[0]['profile']);self.assertIn(',0,1,0)',rows[0]['profile'])
        self.assertNotIn('damroll',rows[0]['profile']);self.assertNotIn('PROJECT_STOP',rows[0]['profile'])
    def test_raw_point_requires_native_target_substitution(self):
        body='int ty,tx;if(!get_aim_dir(&dir))return FALSE;tx=px+99*ddx[dir];ty=py+99*ddy[dir];'
        raw='project(0,2,ty,tx,1,GF_MISSILE,PROJECT_STOP|PROJECT_KILL,-1);'
        _,rows=self.apply(body+raw,core='genso');self.assertFalse(rows[0]['supported'])
        _,rows=self.apply(body+'if((dir==5)&&target_okay()){tx=target_col;ty=target_row;}'+raw,core='genso')
        self.assertTrue(rows[0]['supported']);self.assertIn('PROJECT_STOP|PROJECT_KILL',rows[0]['profile'])
    def test_raw_coordinate_mutation_needs_a_separate_shape(self):
        _,rows=self.apply('int y,x;if(!get_rep_dir2(&dir))return NULL;y=py+ddy[dir];x=px+ddx[dir];x++;project(0,0,y,x,1,GF_FIRE,PROJECT_KILL,-1);',core='genso')
        self.assertFalse(rows[0]['supported'])
    def test_complex_direct_project_stops_at_first_impact(self):
        body=('int y,x;if(!get_rep_dir2(&dir))return NULL;y=py+ddy[dir];x=px+ddx[dir];'
            'if(flag)project(0,randint1(4),y,x,damroll(3,6),GF_UNKNOWN,PROJECT_JUMP|PROJECT_KILL,-1);'
            'project(0,5,y,x,99,GF_FIRE,PROJECT_JUMP|PROJECT_KILL,-1);')
        _,rows=self.apply(body,core='genso')
        self.assertTrue(rows[0]['supported']);self.assertIn('NATIVE_SHAPE_PROJECT',rows[0]['profile'])
        self.assertIn('GF_MISSILE,0,0,PROJECT_JUMP|PROJECT_KILL,1,1,0',rows[0]['profile'])
        self.assertNotIn('randint1',rows[0]['profile']);self.assertEqual(rows[0]['profile'].count('hb_target_profile_effect'),1)
    def test_unrelated_self_projection_is_not_an_impact(self):
        _,rows=self.apply('if(!get_aim_dir(&dir))return;project(0,3,py,px,1,GF_FIRE,PROJECT_JUMP|PROJECT_KILL,-1);',core='genso')
        self.assertFalse(rows[0]['supported'])
    def test_first_impact_accepts_immutable_flags_declared_after_input(self):
        body=('const auto dir=get_aim_dir(player_ptr);if(!dir)return false;'
            'const auto [ty,tx]=dir.get_target_position(player_ptr->get_position(),99);'
            'BIT_FLAGS flags=PROJECT_STOP|PROJECT_GRID|PROJECT_KILL;'
            'for(int i=0;i<count;++i)project(player_ptr,0,player_ptr->lev/20+1,ty,tx,damage,AttributeType::ROCKET,flags);')
        _,rows=self.apply(body,core='heng')
        self.assertTrue(rows[0]['supported']);self.assertIn('NATIVE_SHAPE_PROJECT',rows[0]['profile'])
        self.assertIn('PROJECT_STOP|PROJECT_GRID|PROJECT_KILL',rows[0]['profile'])
    def test_split_beam_stops_at_first_native_prompt(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'r_dragon.c'
            original=('void _split_beam_spell(void){int dir;if(get_fire_dir(&dir)){int e=random_element();'
                'fire_beam(e,dir,1);command_dir=0;if(get_fire_dir(&dir))fire_beam(e,dir,1);}}')
            path.write_text(original,encoding='ascii');_,rows=annotate(path,'frox')
            self.assertTrue(rows[0]['supported']);self.assertIn('NATIVE_SHAPE_BEAM',rows[0]['profile'])
            self.assertFalse(rows[1]['supported'])
    def test_conditional_effect_is_not_presented_as_certain(self):
        _,rows=self.apply('if (!get_fire_dir(&dir)) return; if (randint0(2)) fire_bolt(GF_FIRE,dir,1);')
        self.assertFalse(rows[0]['supported'])
    def test_local_radius_declared_after_input_is_not_evaluated_early(self):
        _,rows=self.apply('if (!get_fire_dir(&dir)) return; int rad=randint1(3); fire_ball(GF_FIRE,dir,1,rad);')
        self.assertFalse(rows[0]['supported'])
    def test_damage_macro_is_not_a_function_owner(self):
        _,rows=self.apply('if (!get_fire_dir(&dir)) return; fire_bolt(GF_FIRE,dir,BOOST(4));',prefix='#define BOOST(n) ((n)*2)\r\n')
        self.assertEqual(rows[0]['function'],'action');self.assertTrue(rows[0]['supported'])
    def test_unknown_device_does_not_get_shape_metadata(self):
        after,rows=self.apply('if (!get_fire_dir(&dir)) return; fire_ball(GF_FIRE,dir,1,2);',name='devices.c')
        self.assertIn('(device_known?',after)
        self.assertIn('hb_target_profile_plain(NATIVE_TARGET_NEUTRAL),hb_target_profile_known(0)',after)
    def test_device_definition_never_replays_original_charge_or_damage(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'cmd-zapwand.cpp'
            original='bool wand_effect(PlayerType *player_ptr,int sval,const Direction &dir,bool powerful,bool magic){int damage=randint1(50);POSITION rad=powerful?3:2;consume_charge();switch(sval){case SV_WAND_FIRE_BALL:fire_ball(player_ptr,AttributeType::FIRE,dir,damage,rad);break;}return true;}'
            path.write_text(original,encoding='ascii');audit=[]
            generated,_=annotate(path,'heng',helpers=ProjectionHelpers(),menu_audit=audit)
            code=generated.decode('ascii');plan=code[code.index('void hb_target_heng_wand_profile'):]
            self.assertIn('powerful?3:2',plan);self.assertNotIn('randint1',plan);self.assertNotIn('consume_charge',plan)
            self.assertEqual(code.count('consume_charge'),1)
    def test_unrecognized_shape_is_not_a_bolt(self):
        _,rows=self.apply('if (!get_fire_dir(&dir)) return; fire_unrecognized_shape(GF_FIRE,dir,1,2,3);')
        self.assertFalse(rows[0]['supported'])
    def test_random_beam_uses_possibilities_without_rolling(self):
        _,rows=self.apply('if (!get_fire_dir(&dir)) return; fire_bolt_or_beam(beam_chance(),GF_FIRE,dir,1);')
        self.assertTrue(rows[0]['supported'])
        self.assertIn('NATIVE_SHAPE_BEAM',rows[0]['profile']);self.assertIn('NATIVE_SHAPE_BOLT',rows[0]['profile'])
        self.assertNotIn('rand',rows[0]['profile'])
    def test_native_random_attribute_table_does_not_read_rolled_index(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'devices.c'
            path.write_text('void do_effect(void){int dir;struct{int type;char *desc;} _choices[2]={{GF_FIRE,"fire"},{GF_COLD,"cold"}};int which=randint0(2);if(!get_fire_dir(&dir))return;fire_ball(_choices[which].type,dir,damroll(4,6),-2);}',encoding='ascii')
            generated,rows=annotate(path,'frox')
            self.assertTrue(rows[0]['supported']);self.assertIn('GF_FIRE',rows[0]['profile']);self.assertIn('GF_COLD',rows[0]['profile'])
            self.assertNotIn('which',rows[0]['profile']);self.assertEqual(generated.count(b'randint0(2)'),1)
    def test_random_beam_branch_keeps_other_branch_geometry_read_only(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'mind-mindcrafter.cpp'
            path.write_text('bool cast_mindcrafter_spell(PlayerType *player_ptr){PLAYER_LEVEL plev=player_ptr->lev;const auto dir=get_aim_dir(player_ptr);if(!dir)return false;if(randint1(100)<plev*2){fire_beam(player_ptr,AttributeType::PSI,dir,Dice::roll(plev,6));}else{fire_ball(player_ptr,AttributeType::PSI,dir,Dice::roll(plev,6),0);}return true;}',encoding='ascii')
            _,rows=annotate(path,'heng')
            self.assertTrue(rows[0]['supported']);self.assertIn('(plev*2)>1',rows[0]['profile']);self.assertIn('(plev*2)<=100',rows[0]['profile']);self.assertNotIn('randint',rows[0]['profile'])
    def test_native_project_hook_flags_are_preserved(self):
        _,rows=self.apply('if (!get_fire_dir(&dir)) return; project_hook(GF_LITE_WEAK,dir,1,PROJECT_BEAM|PROJECT_GRID|PROJECT_KILL);')
        self.assertTrue(rows[0]['supported']);self.assertIn('PROJECT_THRU|(PROJECT_BEAM|PROJECT_GRID|PROJECT_KILL)',rows[0]['profile'])
    def test_local_projection_flags_keep_grid_only_effects_grid_only(self):
        _,rows=self.apply('int flg=PROJECT_BEAM|PROJECT_GRID;if(!get_aim_dir(&dir))return;project_hook(GF_KILL_WALL,dir,1,flg);')
        self.assertTrue(rows[0]['supported']);self.assertIn('PROJECT_GRID',rows[0]['profile']);self.assertNotIn('PROJECT_KILL',rows[0]['profile'])
        _,rows=self.apply('int flg=PROJECT_BEAM|PROJECT_GRID;if(random_flag)flg|=PROJECT_KILL;if(!get_aim_dir(&dir))return;project_hook(GF_KILL_WALL,dir,1,flg);')
        self.assertFalse(rows[0]['supported'])
    def test_multiple_effects_do_not_silently_drop_one(self):
        _,rows=self.apply('if (!get_fire_dir(&dir)) return; fire_bolt(GF_FIRE,dir,1); fire_ball(GF_COLD,dir,1,2);')
        self.assertFalse(rows[0]['supported'])
    def test_native_level_branch_selects_exactly_one_shape(self):
        for core,body in [('frox','if(!get_aim_dir(&dir))return;if(p_ptr->lev<30)fire_bolt(GF_FIRE,dir,1);else fire_ball(GF_FIRE,dir,1,2);'),('heng','const auto dir=get_aim_dir(player_ptr);if(!dir)return false;if(player_ptr->lev<30)fire_bolt(player_ptr,AttributeType::FIRE,dir,1);else fire_ball(player_ptr,AttributeType::FIRE,dir,1,2);')]:
            _,rows=self.apply(body,core=core)
            self.assertTrue(rows[0]['supported']);self.assertIn('NATIVE_SHAPE_BOLT',rows[0]['profile']);self.assertIn('NATIVE_SHAPE_BALL',rows[0]['profile']);self.assertIn('lev<30',rows[0]['profile'])
    def test_unknown_alternate_branch_is_not_dropped(self):
        _,rows=self.apply('if(!get_aim_dir(&dir))return;if(p_ptr->lev<30)fire_bolt(GF_FIRE,dir,1);else fire_unrecognized_shape(GF_FIRE,dir,1,2,3);')
        self.assertFalse(rows[0]['supported'])
    def test_inactive_native_code_is_not_counted_as_a_live_command(self):
        after,rows=self.apply('\n#if 0\nif(!get_aim_dir(&dir))return;fire_unknown(GF_FIRE,dir);\n#elif 1\nif(!get_aim_dir(&dir))return;fire_bolt(GF_FIRE,dir,1);\n#else\nif(!get_aim_dir(&dir))return;fire_unknown(GF_FIRE,dir);\n#endif\n')
        self.assertEqual(len(rows),1);self.assertTrue(rows[0]['supported']);self.assertEqual(after.count('fire_unknown'),2)
    def test_heng_direction_object_cancel_keeps_native_damage(self):
        after,rows=self.apply('const auto dir = get_aim_dir(player_ptr); if (!dir) { return false; } fire_ball(player_ptr,AttributeType::FIRE,dir,damroll(4,6),2);',core='heng')
        self.assertTrue(rows[0]['supported']);self.assertEqual(after.count('damroll(4,6)'),1)
        self.assertIn('NATIVE_SHAPE_BALL',rows[0]['profile'])
    def test_heng_other_condition_is_not_assumed(self):
        _,rows=self.apply('const auto dir = get_aim_dir(player_ptr); if (!other) { return false; } fire_ball(player_ptr,AttributeType::FIRE,dir,1,2);',core='heng')
        self.assertFalse(rows[0]['supported'])
    def test_sil_six_argument_ball_keeps_native_radius(self):
        _,rows=self.apply('if (!get_aim_dir(&dir,0)) return; fire_ball(GF_COLD,dir,5,4,-1,2);',core='sil')
        self.assertTrue(rows[0]['supported']);self.assertIn('GF_COLD,2,MAX_RANGE',rows[0]['profile'])
    def test_level_radius_is_available_before_input(self):
        _,rows=self.apply('int plev=p_ptr->lev; int rad=spell_power(2+plev/10); if (!get_fire_dir(&dir)) return; fire_ball(GF_FIRE,dir,damroll(5,6),rad);')
        self.assertTrue(rows[0]['supported']);self.assertIn('GF_FIRE,rad,',rows[0]['profile'])
    def test_already_rolled_radius_does_not_reveal_the_roll(self):
        _,rows=self.apply('int rad=randint1(4); if (!get_fire_dir(&dir)) return; fire_ball(GF_FIRE,dir,1,rad);')
        self.assertFalse(rows[0]['supported'])
    def test_radius_changed_after_input_is_not_the_pre_input_value(self):
        _,rows=self.apply('int rad=2;if(!get_aim_dir(&dir))return;rad=randint1(3);fire_ball(GF_FIRE,dir,1,rad);')
        self.assertFalse(rows[0]['supported'])
    def test_modified_radius_requires_its_own_review(self):
        _,rows=self.apply('int rad=2; modify(&rad); if (!get_fire_dir(&dir)) return; fire_ball(GF_FIRE,dir,1,rad);')
        self.assertFalse(rows[0]['supported'])
    def test_separate_level_assignment_is_read_without_replaying_it(self):
        _,rows=self.apply('int plev; plev=p_ptr->lev; if(!get_aim_dir(&dir))return; fire_ball(GF_FIRE,dir,1,plev/10);')
        self.assertTrue(rows[0]['supported'])
    def test_case_local_assignment_does_not_take_another_cases_value(self):
        _,rows=self.apply('int rad;switch(spell){case 0:rad=randint1(3);break;case 1:rad=2;if(!get_aim_dir(&dir))return;fire_ball(GF_FIRE,dir,1,rad);break;}')
        self.assertTrue(rows[0]['supported'])
        _,rows=self.apply('int rad=2;if(random_flag)rad=randint1(3);if(!get_aim_dir(&dir))return;fire_ball(GF_FIRE,dir,1,rad);')
        self.assertFalse(rows[0]['supported'])
    def test_local_from_closed_scope_is_not_read(self):
        _,rows=self.apply('if (flag) {int rad=2;} if (!get_fire_dir(&dir)) return; fire_ball(GF_FIRE,dir,1,rad);')
        self.assertFalse(rows[0]['supported'])
    def test_heng_const_level_radius(self):
        _,rows=self.apply('const auto plev=player_ptr->lev; const auto dir=get_aim_dir(player_ptr); if (!dir) return false; fire_ball(player_ptr,AttributeType::FIRE,dir,1,2+plev/10);',core='heng')
        self.assertTrue(rows[0]['supported'])
    def test_heng_value_locals_and_native_beam_chance(self):
        _,rows=self.apply('PLAYER_LEVEL plev=player_ptr->lev; POSITION rad=2+plev/10; if(info)return info_radius(rad); const auto dir=get_aim_dir(player_ptr); if(!dir)return false; fire_ball(player_ptr,AttributeType::FIRE,dir,1,rad);',core='heng')
        self.assertTrue(rows[0]['supported'])
        _,rows=self.apply('const auto dir=get_aim_dir(player_ptr); if(!dir)return false; fire_bolt_or_beam(player_ptr,beam_chance(player_ptr)-10,AttributeType::FIRE,dir,1);',core='heng')
        self.assertTrue(rows[0]['supported']);self.assertIn('NATIVE_SHAPE_BEAM',rows[0]['profile'])
    def test_heng_implicit_reference_is_not_hoisted(self):
        _,rows=self.apply('POSITION rad=2; mutate(rad); const auto dir=get_aim_dir(player_ptr); if(!dir)return false; fire_ball(player_ptr,AttributeType::FIRE,dir,1,rad);',core='heng')
        self.assertFalse(rows[0]['supported'])
    def test_hidden_ball_keeps_native_geometry(self):
        _,rows=self.apply('if (!get_fire_dir(&dir)) return; fire_ball_hide(GF_STASIS,dir,7,0);')
        self.assertTrue(rows[0]['supported']);self.assertIn('NATIVE_SHAPE_BALL,GF_STASIS,0',rows[0]['profile']);self.assertIn('PROJECT_HIDE',rows[0]['profile'])
    def test_support_effect_sets_green_kind(self):
        _,rows=self.apply('if (!get_fire_dir(&dir)) return; project_hook(GF_OLD_HEAL,dir,damroll(4,6),PROJECT_STOP|PROJECT_KILL);')
        self.assertTrue(rows[0]['supported']);self.assertIn('NATIVE_TARGET_SUPPORT',rows[0]['profile']);self.assertNotIn('damroll',rows[0]['profile'])
    def test_projection_result_condition_does_not_hide_the_effect(self):
        _,rows=self.apply('if(!get_buff_mon_dir(&dir))return; if(project_hook(GF_OLD_HEAL,dir,1,PROJECT_STOP|PROJECT_KILL))noticed=1;')
        self.assertTrue(rows[0]['supported']);self.assertIn('NATIVE_TARGET_SUPPORT',rows[0]['profile'])
    def test_short_circuit_condition_is_not_ignored(self):
        _,rows=self.apply('if(!get_aim_dir(&dir))return; if(flag&&fire_ball(GF_FIRE,dir,1,2))noticed=1;')
        self.assertFalse(rows[0]['supported'])
    def test_special_mirror_geometry_has_a_bounded_turn_preview(self):
        _,rows=self.apply('if (!get_fire_dir(&dir)) return; fire_beam(GF_SEEKER,dir,7);')
        self.assertTrue(rows[0]['supported']);self.assertIn('NATIVE_SHAPE_MIRROR_RAY',rows[0]['profile'])
        self.assertNotIn('NATIVE_SHAPE_BEAM',rows[0]['profile'])
    def test_native_helper_uses_geometry_and_leaves_rng_at_action(self):
        with tempfile.TemporaryDirectory() as directory:
            helper=Path(directory)/'native.c'
            helper.write_text('bool heal_monster(int dir,int dam) { int flg=PROJECT_STOP|PROJECT_KILL; return project_hook(GF_OLD_HEAL,dir,dam,flg); }')
            catalog=ProjectionHelpers();catalog.read(helper)
            source=Path(directory)/'action.c';source.write_text('void action(void) {int dir; if(!get_aim_dir(&dir))return; heal_monster(dir,damroll(4,6));}')
            after,rows=annotate(source,helpers=catalog)
            self.assertTrue(rows[0]['supported']);self.assertIn('NATIVE_TARGET_SUPPORT',rows[0]['profile']);self.assertEqual(after.count(b'damroll(4,6)'),1)
    def test_conditional_wrapper_requires_separate_handling(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'native.c';path.write_text('bool guarded(int dir) {if(flag)return false;return fire_bolt(GF_FIRE,dir,7);}')
            catalog=ProjectionHelpers();catalog.read(path)
            self.assertIsNone(catalog.expand('guarded',['dir']))
    def test_heng_helper_flags_are_preserved(self):
        _,rows=self.apply('const auto dir=get_aim_dir(player_ptr); if(!dir)return false; project_hook(player_ptr,AttributeType::KILL_WALL,dir,1,PROJECT_BEAM|PROJECT_GRID|PROJECT_KILL);',core='heng')
        self.assertTrue(rows[0]['supported']);self.assertIn('NATIVE_SHAPE_BEAM',rows[0]['profile'])
    def test_blast_has_random_footprint_without_damage_evaluation(self):
        after,rows=self.apply('if (!get_fire_dir(&dir)) return; fire_blast(GF_LITE,dir,damroll(3,6),2,10,3);')
        self.assertTrue(rows[0]['supported']);self.assertIn('NATIVE_SHAPE_BLAST',rows[0]['profile']);self.assertNotIn('damroll',rows[0]['profile']);self.assertEqual(after.count('damroll'),1)
    def test_genso_scattered_beams_keep_overridden_flags(self):
        _,rows=self.apply('if (!get_aim_dir(&dir)) return; fire_blast(GF_COLD,dir,2,10,3,4,(PROJECT_BEAM|PROJECT_KILL|PROJECT_GRID|PROJECT_ITEM));',core='genso')
        self.assertTrue(rows[0]['supported']);self.assertNotIn('PROJECT_STOP',rows[0]['profile']);self.assertNotIn('PROJECT_THRU',rows[0]['profile'])

if __name__=='__main__':unittest.main()
