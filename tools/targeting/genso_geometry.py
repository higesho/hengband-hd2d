"""Copy native Gensoband geometry and substitute read-only knowledge queries."""
import re

def generate(spells,cave):
    def function(raw,signature):
        text=raw.decode('latin1').replace('\r\n','\n')
        start=text.index(signature);stop=text.index('\n}',start)+2
        return text[start:stop]
    path=function(spells,'sint project_path(').replace('sint project_path(','static sint _gb_preview_project_path(',1)
    los=function(cave,'bool los(').replace('bool los(','static bool _gb_preview_los(',1)
    disi=function(spells,'bool in_disintegration_range(').replace('bool in_disintegration_range(','static bool _gb_preview_disintegration_range(',1)
    breath=function(spells,'void breath_shape(').replace('void breath_shape(','static void _gb_preview_breath_shape(',1)
    text='\n'.join((path,los,disi,breath))
    text=text.replace('cave_stop_disintegration(', '_gb_preview_stop_disintegration(')
    text=text.replace('cave_los_bold(', '_gb_preview_los_cell(')
    text=re.sub(r'cave_have_flag_bold\(\s*y\s*,\s*x\s*,\s*FF_PROJECT\s*\)',r'_gb_preview_project_cell(y,x)',text)
    text=text.replace('cave[y][x].m_idx != 0','_gb_preview_monster(y,x) != 0')
    text=re.sub(r'\blos\(', '_gb_preview_los(',text)
    text=re.sub(r'\bin_disintegration_range\(', '_gb_preview_disintegration_range(',text)
    text=re.sub(r'\bprojectable\(', '_gb_preview_projectable(',text)
    # 2.1.7 added a wizard-only trace to breath_shape(); the preview must stay silent.
    text=re.sub(r'\n[ \t]*if\s*\(\s*p_ptr->wizard\s*\)\s*\n[ \t]*msg_format\("dist:%d mdis:%d bdis:%d"[^;]*\);','',text)
    if re.search(r'\bcave\[|\brand\w*\(',text):raise ValueError('Unreviewed mutable geometry dependency')
    if re.search(r'\bmsg_\w+\(',text):raise ValueError('Unreviewed message in native geometry')
    notices=[]
    for raw in (spells,cave):
        for comment in re.findall(r'/\*.*?\*/',raw.decode('latin1'),re.S):
            if 'copyright' in comment.lower() and comment not in notices:notices.append(comment)
    return ('\n'.join(notices)+'\n/* Source-derived native geometry; no hidden grid or creature queries. */\n'+text+'\n').encode('latin1')
