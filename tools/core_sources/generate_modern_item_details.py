from pathlib import Path
import argparse
parser=argparse.ArgumentParser()
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from paths import prepared_root,generated_root,write_if_changed
parser.add_argument("--source-root",type=Path,default=None,help="prepared-source root (default: HENGBAND_CORE_SOURCE_ROOT or ../roguelike-cores/build-sources)")
parser.add_argument("--output-root",type=Path,default=None,help="generated root (default: <source-root>-generated); writes <output-root>/presentation/bridge/")
parser.add_argument("--check",action="store_true")
parser.add_argument("--variant",choices=("hengband","tangband"),default="hengband",help="variant: tangband reads tangband/src and writes <output-root>/tangband/presentation/bridge/ (Tangband keeps the Hengband 3.0.2.4 source)")
args=parser.parse_args()
if args.source_root is None:args.source_root=prepared_root()
if args.output_root is None:args.output_root=generated_root(args.source_root)
from hashlib import sha256
import re

source=args.source_root/('tangband/src' if args.variant=='tangband' else 'src')/'perception/identification.cpp'
s=source.read_text(encoding='utf-8-sig')
begin=s.index('    std::array<std::string, 128> info{};')
end=s.index('    if (mode & SCROBJ_FORCE_DETAIL) {',begin)
body=s[begin:end]
# Copy the explanation assembly only, never screen_save, inkey or screen_load.
# An auto-growing list removes the original 128-line limit.
body=body.replace('std::array<std::string, 128> info{};', 'std::vector<std::string> info;')
body=body.replace('    auto trivial_info = 0;\n','').replace('    auto i = 0;\n','')
body=re.sub(r'    std::transform\(item_text_lines.begin\(\), item_text_lines.end\(\), &info\[i\], .*?\n', '    for (const auto &line : item_text_lines) info.push_back(line.data());\n',body)
body=body.replace('    i += item_text_lines.size();\n','')
body=re.sub(r'    if \(item.is_equipment\(\)\) \{\n        trivial_info = i;\n    \}\n','',body)
body=body.replace('            trivial_info++;\n','')
body=re.sub(r'info\[i\+\+\] = ([^;]+);',r'info.push_back(\1);',body)
body=body.replace('const auto item_text = item.is_fixed_artifact() ? item.get_fixed_artifact().text.data() : item.get_baseitem().text.data();','const auto item_text = localized_lore ? localized_lore : (item.is_fixed_artifact() ? item.get_fixed_artifact().text.data() : item.get_baseitem().text.data());')
body=body.replace('    const auto item_text_lines = shape_buffer(item_text, 77 - 15);\n    for (const auto &line : item_text_lines) info.push_back(line.data());','    if (*item_text) info.push_back(item_text);')
if 'info[' in body or 'trivial_info' in body:raise RuntimeError('incomplete extraction')
includes=s[:s.index('/*!')]
includes='\n'.join(x for x in includes.splitlines() if x.startswith('#include') and not any(k in x for k in ['input-key-acceptor.h','screen-processor.h']))
partial=[]
for flag,expr in re.findall(r'if \(flags.has\((TR_\w+)\)\) \{\s*info\[i\+\+\] = (_\("[^"\n]*", "[^"\n]*"\));\s*\}',s[:end]):
    if flag not in [x[0] for x in partial]:partial.append((flag,expr))
generated='''// Generated from the explanation section of src/perception/identification.cpp.
// Source SHA256: '''+sha256(source.read_bytes()).hexdigest()+'''
// See tools/core_sources/generate_modern_item_details.py. No core source is modified.
#pragma once
'''+includes+'''
#include <vector>
namespace presentation {
inline std::vector<std::string> modern_known_item_details(const ItemEntity &item,const char *localized_lore=nullptr) {
    if (!item.is_fully_known()) {
        std::vector<std::string> info;
        const auto flags = item.get_flags_known();
'''+''.join('        if (flags.has('+f+')) info.push_back('+e+');\n' for f,e in partial)+'''
        return info;
    }
    constexpr BIT_FLAGS mode = SCROBJ_FORCE_DETAIL;
'''+body+'''
    if (item.bi_key == BaseitemKey(ItemKindType::STATUE, SV_PHOTO)) {
        const auto &monrace = item.get_monrace();
        info.insert(info.begin(), monrace.name.data());
    }
    return info;
}
}
'''
target=args.output_root/('tangband/' if args.variant=='tangband' else '')/'presentation/bridge/modern_item_details.generated.h'
if args.check:
    if not target.is_file() or target.read_text(encoding='utf-8-sig') != generated:
        raise SystemExit('Generated item explanation header is missing or stale: '+str(target))
    print('Item explanation source and generated header match.')
else:
    write_if_changed(target,generated)
