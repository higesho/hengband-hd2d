/* Included from cave.c so the copied, read-only projection uses native vinfo. */
int sq_target_grid_known(int y,int x)
{
    return in_bounds_fully(y,x) && (cave_info[y][x]&(CAVE_MARK|CAVE_SEEN));
}
static u32b sq_target_known_info(int y,int x)
{
    return sq_target_grid_known(y,x)?cave_info[y][x]:CAVE_FIRE;
}
static byte sq_target_known_feat(int y,int x)
{
    return sq_target_grid_known(y,x)?cave_feat[y][x]:0;
}
int sq_target_grid_open(int y,int x)
{
    return in_bounds_fully(y,x) && (!sq_target_grid_known(y,x)||cave_floor_bold(y,x));
}
int sq_target_visible_monster(int y,int x)
{
    int index;
    if (!sq_target_grid_known(y,x)) return 0;
    index=cave_m_idx[y][x];
    if (index<0) return index;
    return index && mon_list[index].ml && !p_ptr->image?index:0;
}
