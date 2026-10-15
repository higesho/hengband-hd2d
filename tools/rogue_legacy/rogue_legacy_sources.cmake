# Source lists for the restored Rogue 3.6.4 / 5.4.4 cores.
#
# Keep these explicit.  The upstream trees also contain helper programs such as
# scedit/findpw; a broad *.c glob would accidentally compile their extra main()
# functions into the game core.

set(HB_ROGUE36_C_FILES
    vers.c armor.c chase.c command.c daemon.c daemons.c fight.c init.c io.c
    list.c main.c mdport.c misc.c monsters.c move.c newlevel.c options.c pack.c
    passages.c potions.c rings.c rip.c rooms.c save.c scrolls.c state.c sticks.c
    things.c weapons.c wizard.c xcrypt.c)

set(HB_ROGUE54_C_FILES
    vers.c extern.c armor.c chase.c command.c daemon.c daemons.c fight.c init.c
    io.c list.c mach_dep.c main.c mdport.c misc.c monsters.c move.c new_level.c
    options.c pack.c passages.c potions.c rings.c rip.c rooms.c save.c scrolls.c
    state.c sticks.c things.c weapons.c wizard.c xcrypt.c)

function(hb_rogue_legacy_collect out_var source_dir generation)
    if(generation STREQUAL "36")
        set(names ${HB_ROGUE36_C_FILES})
    elseif(generation STREQUAL "54")
        set(names ${HB_ROGUE54_C_FILES})
    else()
        message(FATAL_ERROR "unknown Rogue legacy generation: ${generation}")
    endif()

    set(files)
    foreach(name IN LISTS names)
        list(APPEND files "${source_dir}/${name}")
    endforeach()
    set(${out_var} "${files}" PARENT_SCOPE)
endfunction()
