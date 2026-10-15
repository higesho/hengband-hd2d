from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]


def text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8-sig")


def block(source: str, marker: str, end_marker: str) -> str:
    start = source.index(marker)
    end = source.index(end_marker, start)
    return source[start:end]


checks = 0


def check(condition: bool, label: str) -> None:
    global checks
    if not condition:
        raise AssertionError(label)
    checks += 1


# Hengband / Tangband: the Modern command surface is a direct flattening of the
# original menu_info table.  Audit-only raw-key supplements must not creep back.
shared = text("presentation/bridge/pad_command_table.cpp")
register = block(shared, "void register_pad_command_table()", "} // namespace presentation")
check("menu_info[0][g]" in register and "menu_info[sub][k]" in register,
      "Heng/Tang command menu must still come from the original menu_info table")
check("append_missing" not in register,
      "Heng/Tang must not re-add audit-only raw-key commands to the top menu")
# The original commands that menu_info omits: repeat last command ('n',
# src/io/command-repeater.cpp) and the repeat count ('0',
# src/io/input-key-requester.cpp process_repeat_num).  Pad/touch-only builds
# have no other route.
check(register.count("entry.command = static_cast<uint8_t>(") == 3
      and "entry.command = static_cast<uint8_t>('n');" in register
      and "count_entry.command = static_cast<uint8_t>('0');" in register,
      "Heng/Tang may add only the original repeat and repeat-count commands outside menu_info")

# Command ids are saved in cfg (key_bind= / pad_bind=) and the UI reads 0 as "no binding"
# (hd2d/ui/key_binds.h kActionNone).  Every id must go through allocate_entry_id() so the
# first entry never gets 0.  The live per-core check is pad_command_ids_check.py.
check("next_id++" not in register and register.count("allocate_entry_id(next_id)") == 3,
      "Heng/Tang command ids must come from allocate_entry_id (id 0 is reserved for 'no binding')")


# Gensoband: preserve every original menu_info row.  Only the two historical
# non-menu commands that existed before the audit cleanup remain as extras.
genso = text("gensoband/adapter/gb_pad_commands.cpp")
genso_core = block(genso, "std::vector<presentation::PadCommandWireEntry> entries_from_core()", "//! 落ち先の最小表")
genso_extras = block(genso, "const PadExtra kExtras[] = {", "};")
check("if ((src.command" not in genso_core and "if (src.command" not in genso_core
      and "out.push_back(std::move(wire))" in genso_core,
      "Gensoband must not filter original menu_info rows")
check(re.findall(r"\{\s*([^,]+),", genso_extras.split("{", 1)[1]) == ["7", "'.'", "'n'", "'0'"],
      "Gensoband extras must remain only Barrier, Run, Repeat and Repeat count")


# Frox: likewise copy the original menu table without the old stale-row filter;
# Run(.) is the only pre-audit supplemental row.
frox = text("frox/adapter/fc_pad_commands.cpp")
frox_core = block(frox, "std::vector<presentation::PadCommandWireEntry> entries_from_core()", "//! 落ち先の最小表")
frox_extras = block(frox, "const PadExtra kExtras[] = {", "};")
check("src.command ==" not in frox_core and "out.push_back(std::move(wire))" in frox_core,
      "Frox must not filter original menu_info rows")
check(re.findall(r"\{\s*([^,]+),", frox_extras.split("{", 1)[1]) == ["'.'", "'n'", "'\"'", "'\\''", "'0'"],
      "Frox extras must remain only Run(.), Repeat(n), Record(\"), Playback(') and Repeat count(0)")


# Host ownership invariant: keyboard, physical-pad and virtual-pad entry points
# converge on one guard before a root menu can open.  Back becomes cancel while a
# child owner is active instead of opening another root UI over it.
app = text("hd2d/app/hd2d_app.cpp")
guard = block(app, "const auto top_menu_available =", "const auto perform_action")
for required in (
    "!frame.menu_open", "!frame.pre_game_menu", "!frame.text_input_active",
    "!frame.numeric.active", "frame.prompt.choices.empty()",
    "!hud_state.modern.active()", "!hud_state.targeting.active()",
):
    check(required in guard, f"top-menu ownership guard missing {required}")
check(app.count("if (!top_menu_available())") >= 3,
      "feature menu, command menu and controller Back must share the ownership guard")
check('wire.e = "cancel";' in block(app, "if (pressed == PadInput::Back)", "if (pressed == PadInput::A)"),
      "controller Back must cancel rather than open a root menu during child input")

# Rogue settings are a first-level tab.  Selecting that tab dispatches the
# native `o` settings command directly; there must not be another one-row
# "original settings" doorway under it.
r2 = text("rogue2/adapter/r2_main.cpp")
rl = text("rogue_legacy/adapter/rl_main.cpp")
feature = text("hd2d/ui/feature_menu.cpp")
modern = text("hd2d/ui/modern_store_ui.cpp")
modern_h = text("hd2d/ui/modern_store_ui.h")
input_controller = text("hd2d/app/input_controller.cpp")
check("'o', \"設定\"" in r2 and "\"原作設定\"" not in r2,
      "RogueClone2 settings must be a first-level Settings category")
check("'o', \"設定\"" in rl and "\"原作設定\"" not in rl,
      "Rogue 3.6/5.4 settings must be a first-level Settings category")
check('command_tabs_.tabs()[command_tabs_.index()].id=="設定"' in feature
      and 'command.group_utf8=="設定"&&command.command==\'o\'' in feature,
      "Settings tab must dispatch the Rogue settings command directly")
check("set_parent_command_tabs(parent_tabs,parent_tab)" in app
      and "take_parent_tab_request()" in app
      and "feature_menu.open_command_tab(parent_tab,settings)" in app,
      "Rogue settings must retain and restore the parent command-tab context")
check("parent_tab_navigation_active()" in modern_h
      and 'send("cancel",buffered_)' in block(modern, "void ModernStoreUi::tab", "void ModernStoreUi::open_inventory")
      and "if(parent_tab_request_<0||active()||pending_)return -1" in modern,
      "Rogue parent-tab switching must close the native child before reopening the parent tab")
check("parent_tab_navigation_active()" in input_controller
      and modern.count("parent_tab_navigation_active()") >= 4,
      "Rogue settings must expose parent-tab navigation to keyboard, pad and clickable headers")
check('event.e == "move"' in r2 and "direction_continue_marker_visible()" in r2 and "keys.push_back(' ')" in r2,
      "RogueClone2 direction input must advance only an explicit [続く] marker")
check('event.e == "move"' in rl and "direction_continue_marker_visible()" in rl and "keys.push_back(' ')" in rl,
      "Rogue 3.6/5.4 direction input must advance only an explicit [続く] marker")


print(f"PASS {checks} command-menu cleanup/static ownership checks")
