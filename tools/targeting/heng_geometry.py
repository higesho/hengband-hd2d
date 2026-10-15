"""Reproduce the native read-only geometry against knowledge-limited views."""
import re
import line_anchor as anchor

# Hashes of original lines (see line_anchor.py); the original text is not quoted here.
FORWARD_DECL='8a762360060e'
FLOOR_LOCAL='4e9ed185fe43'

FILES=('target/projection-path-calculator.h','target/projection-path-calculator.cpp',
       'floor/line-of-sight.cpp','spell/range-calc.h','spell/range-calc.cpp')

def generate(root):
    sources={name:(root/name).read_bytes() for name in FILES}
    def adapt(name):
        text=sources[name].decode('latin1').replace('\r\n','\n')
        text=re.sub(r'^#(?:include|pragma)[^\n]*\n','',text,flags=re.M)
        text=anchor.edit_all(text,FORWARD_DECL,lambda lines:[''])[0]
        text=text.replace('PlayerType *player_ptr','const HengTargetFloor &floor')
        text=anchor.edit_all(text,FLOOR_LOCAL,lambda lines:[])[0]
        text=text.replace('is_prevent_blast(player_ptr,','is_prevent_blast(floor,')
        for old,new in [('ProjectionPathHelper','HengTargetPathHelper'),('ProjectionPath','HengTargetPath'),
                        ('FloorType','HengTargetFloor'),('projectable','heng_target_projectable'),
                        ('in_disintegration_range','heng_target_disintegration'),('los','heng_target_los'),
                        ('is_prevent_blast','heng_target_prevent_blast'),('dist_to_line','heng_target_dist_to_line'),
                        ('breath_shape','heng_target_breath_shape'),('ball_shape','heng_target_ball_shape')]:
            text=re.sub(r'\b'+old+r'\b',new,text)
        if name=='target/projection-path-calculator.cpp':
            # Native project() resets project_length before calling ball/breath
            # visibility. The preview must not reset the real game variable.
            old='project_length ? project_length : AngbandSystem::get_instance().get_max_range()'
            if text.count(old)!=1:raise ValueError('Heng native visibility range changed')
            text=text.replace(old,'AngbandSystem::get_instance().get_max_range()')
        return '// Derived from '+name+'; original notices retained.\n'+text
    order=(FILES[0],FILES[3],FILES[2],FILES[1],FILES[4])
    return ('/* Native geometry with knowledge-limited floor predicates. */\n'+''.join(adapt(name) for name in order)).encode('latin1'),sources
