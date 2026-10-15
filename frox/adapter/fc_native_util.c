/* Keep the external core unchanged; replace its obsolete temporary path glue. */
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#define my_fopen_temp fc_original_fopen_temp
#include "util.c"
#undef my_fopen_temp
#include "fc_native_store_common.h"
void fc_native_item_use_commands(const void *,native_store_item *);

void fc_native_describe_item(const void *source,native_store_item *out)
{
    const obj_t *obj=(const obj_t *)source;
    fc_native_item((obj_ptr)obj,out);
    out->slot=obj->loc.slot;out->location=obj->loc.where==INV_EQUIP?1:(obj->loc.where==INV_FLOOR?2:(obj->loc.where==INV_QUIVER?3:0));
    native_item_command(out,'I');native_item_command(out,'{');
    if(obj->inscription)native_item_command(out,'}');
    native_item_command(out,'d');native_item_command(out,'k');
    if(out->location==1)native_item_command(out,'t');
    else {
        native_item_command(out,'v');
        if(object_is_wearable((obj_ptr)obj))native_item_command(out,'w');
    }
    if(obj_is_ammo((obj_ptr)obj))native_item_command(out,'f');
    if(obj_is_readable_book((obj_ptr)obj)){native_item_command(out,'b');native_item_command(out,'m');native_item_command(out,'G');}
    if(obj->tval==TV_FLASK||obj->tval==TV_LITE)native_item_command(out,'F');
    fc_native_item_use_commands(source,out);
}
int fc_native_owned_item(int index,native_store_item *out)
{
    int location,slot;obj_ptr obj=fc_native_own(index,&location,&slot);
    if(!obj)return 0;fc_native_describe_item(obj,out);out->key=slot_label(slot);
    /* 破壊（k）の選択肢は装備を含まない。装備の行に出すと持ち物メニューが行を探して諦める（2026-09-25）。 */
    if(out->location==1)native_item_drop_command(out,'k');
    return 1;
}

/* Read only: movement keymaps already carry their chosen direction. */
int fc_target_mapped_direction(void)
{
    if (!inkey_next || !*inkey_next || inkey_xtra) return 0;
    return get_keymap_dir(*inkey_next, FALSE);
}

FILE *my_fopen_temp(char *buf, int max)
{
    FILE *file;
    if (!buf || max < 1) return NULL;
    buf[0] = '\0';
#ifdef _WIN32
    {
        char name[MAX_PATH];
        const char *directory = ANGBAND_DIR_USER;
        if (!directory || max < MAX_PATH) return NULL;
        if (!GetTempFileNameA(directory, "hdf", 0, name)) return NULL;
        if ((int)strlen(name) >= max) { DeleteFileA(name); return NULL; }
        strcpy(buf, name);
        file = my_fopen(buf, "w");
        if (!file) { DeleteFileA(name); buf[0] = '\0'; }
    }
#else
    {
        int fd;
        const char *directory = ANGBAND_DIR_USER;
        if (!directory || snprintf(buf, max, "%s/hd2d-XXXXXX", directory) >= max) { buf[0] = '\0'; return NULL; }
        fd = mkstemp(buf);
        if (fd < 0) { buf[0] = '\0'; return NULL; }
        file = fdopen(fd, "w");
        if (!file) { close(fd); unlink(buf); buf[0] = '\0'; }
    }
#endif
    return file;
}
