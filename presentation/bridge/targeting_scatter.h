#pragma once

/* The three native fire_blast implementations draw each endpoint with
 * rand_spread, rejecting points outside their native distance radius.
 * Enumerate exactly those integer endpoints without reading or advancing RNG.
 * A callback computes that core's own path, visibility and collisions.
 */
typedef int (*hb_scatter_distance)(int,int,int,int);
typedef int (*hb_scatter_visit)(int,int,void *);
static int hb_target_scatter_points(int sy,int sx,int ty,int tx,int direction,int deviation,
    hb_scatter_distance metric,hb_scatter_visit visit,void *context)
{
    static const int dx[10]={0,-1,0,1,-1,0,1,-1,0,1};
    static const int dy[10]={0,1,1,1,0,0,0,-1,-1,-1};
    int cy,cx,spread,y,x;
    if(direction<1||direction>9||deviation<0||deviation>64)return 0;
    cy=sy+20*(direction==5?ty-sy:dy[direction]);
    cx=sx+20*(direction==5?tx-sx:dx[direction]);
    spread=metric(sy,sx,cy,cx)*deviation/20;
    if(spread>4096)return 0;
    for(y=cy-spread;y<=cy+spread;++y)for(x=cx-spread;x<=cx+spread;++x)
        if(metric(cy,cx,y,x)<=spread&&!visit(y,x,context))return 0;
    return 1;
}
