import tempfile,unittest
from pathlib import Path
from profiles import annotate
from projection_helpers import ProjectionHelpers
from profile_inventory import Source
from self_profiles import scatter_plan

class ItemProfiles(unittest.TestCase):
    def derive(self,name,body,core):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/name;path.write_text(body,encoding='ascii');audit=[]
            after,_=annotate(path,core,helpers=ProjectionHelpers(),menu_audit=audit)
            self.assertEqual(path.read_text(encoding='ascii'),body)
            return after.decode('ascii'),audit

    def test_staff_random_radius_is_possible_without_rolling(self):
        original='int staff_effect(PlayerType *player_ptr,int sval,bool *use_charge,bool powerful,bool magic,bool known){consume_charge();switch(sval){case SV_STAFF_DESTRUCTION:destroy_area(player_ptr,player_ptr->y,player_ptr->x,(powerful?18:13)+randint0(5),false);break;}return 1;}'
        code,audit=self.derive('cmd-usestaff.cpp',original,'heng')
        plan=code[code.index('void hb_target_heng_staff_profile'):]
        self.assertEqual(plan.count('NATIVE_SHAPE_DESTRUCTION'),2)
        self.assertIn('powerful?18:13',plan);self.assertIn('+4,0,0,1,0,0)',plan)
        self.assertNotIn('randint0',plan);self.assertNotIn('consume_charge',plan)
        self.assertEqual(code.count('randint0'),1);self.assertEqual(code.count('consume_charge'),1)

    def test_staff_light_keeps_radius_and_room_as_separate_footprints(self):
        original=('int staff_effect(PlayerType *player_ptr,int sval,bool *use_charge,bool powerful,bool magic,bool known)'
            '{switch(sval){case SV_STAFF_LITE:lite_area(player_ptr,Dice::roll(2,8),powerful?4:2);break;}return 1;}')
        code,_=self.derive('cmd-usestaff.cpp',original,'heng');plan=code[code.index('void hb_target_heng_staff_profile'):]
        self.assertIn('NATIVE_SHAPE_BALL',plan);self.assertIn('NATIVE_SHAPE_ROOM_AREA',plan)
        self.assertIn('powerful?4:2',plan);self.assertNotIn('Dice::roll',plan)

    def test_frox_override_uses_original_radius_without_action_preamble(self):
        original='static int _extra(effect_t *effect,int def){int result=effect->extra;if(!result)result=def;return result;} cptr do_effect(effect_t *effect,int mode,int boost){bool cast=mode==SPELL_CAST;consume_charge();switch(effect->type){case EFFECT_EARTHQUAKE:if(cast){earthquake(py,px,_extra(effect,10));}break;case EFFECT_FIRE:if(cast){int dir;if(!get_fire_dir(&dir))return 0;fire_ball(GF_FIRE,dir,1,3);}break;}return 0;}'
        code,audit=self.derive('devices.c',original,'frox');plan=code[code.index('void fc_target_device_profile'):]
        self.assertIn('(effect->extra?effect->extra:10)',plan)
        self.assertNotIn('consume_charge',plan);self.assertNotIn('EFFECT_FIRE',plan)
        self.assertEqual(code.count('consume_charge'),1)

    def test_aimed_rod_never_becomes_a_self_preview(self):
        original='bool rod_effect(PlayerType *player_ptr,int sval,const Direction &dir,bool powerful,bool magic){switch(sval){case SV_ROD_FIRE_BALL:fire_ball(player_ptr,AttributeType::FIRE,dir,200,3);break;}return true;}'
        _,audit=self.derive('cmd-zaprod.cpp',original,'heng')
        fixed=next(row for row in audit if row['function']=='hb_target_heng_rod_self_profile')
        aimed=next(row for row in audit if row['function']=='hb_target_heng_rod_profile')
        self.assertIsNone(fixed['cases'][0]['profile']);self.assertIsNotNone(aimed['cases'][0]['profile'])

    def test_native_starlight_scatter_is_read_without_rolling(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'spells-lite.cpp'
            path.write_text('bool starlight(PlayerType *player_ptr,bool magic){const auto p_pos=player_ptr->get_position();const auto num=Dice::roll(5,3);for(auto k=0;k<num;k++){Pos2D pos(0,0);auto attempts=1000;while(attempts--){pos=scatter(*player_ptr->current_floor_ptr,p_pos,4,PROJECT_LOS);if(!player_ptr->current_floor_ptr->has_terrain_characteristics(pos,TerrainCharacteristics::PROJECTION))continue;if(!player_ptr->is_located_at(pos))break;}constexpr uint flags=PROJECT_BEAM|PROJECT_THRU|PROJECT_GRID|PROJECT_KILL|PROJECT_LOS;project(player_ptr,0,0,pos.y,pos.x,Dice::roll(6,10),AttributeType::LITE_WEAK,flags);}return true;}',encoding='ascii')
            source=Source(path);owner=next(f for f in source.functions if f[2]=='starlight')
            plan=scatter_plan(source,owner[0],owner[1],owner,'heng')
            self.assertEqual(plan,['AttributeType::LITE_WEAK','4','PROJECT_BEAM|PROJECT_THRU|PROJECT_GRID|PROJECT_KILL|PROJECT_LOS','1'])

    def test_sil_radiance_requires_a_known_bow(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'cmd2.c'
            original='void do_cmd_fire(void){object_type *j_ptr;u32b f2;int dir,tdis;if(!get_aim_dir(&dir,tdis))return;}'
            path.write_text(original,encoding='ascii');after,rows=annotate(path,'sil')
            self.assertTrue(rows[0]['supported'])
            self.assertIn('object_known_p(j_ptr)&&(f2&TR2_RADIANCE)',rows[0]['profile'])

if __name__=='__main__':unittest.main()
