#include "render/targeting_visual.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace hd2d {
namespace {
TargetingQuad colored(const TargetingState &s) {
    TargetingQuad q;
    if(s.kind=="attack"){q.r=1.f;q.g=.16f;q.b=.12f;}
    else if(s.kind=="support"){q.r=.12f;q.g=1.f;q.b=.32f;}
    else {q.r=.95f;q.g=.97f;q.b=1.f;}
    return q;
}
Vec3 toward_eye(Vec3 eye,float x,float y) {
    Vec3 normal=normalize(Vec3{eye.x-x,eye.y-y,0.f});if(dot(normal,normal)<.01f)normal={0.f,1.f,0.f};return normal;
}
Vec3 body_normal(const TargetingEntity &body,Vec3 eye) {
    if(dot(body.normal,body.normal)<.01f)return toward_eye(eye,body.x,body.y);
    auto normal=normalize(body.normal);
    if(dot(normal,Vec3{eye.x-body.x,eye.y-body.y,0})<0)normal=normal*-1.f;
    return normal;
}
}
TargetingVisual build_targeting_visual(const TargetingState &s,Vec3 eye,std::span<const TargetingEntity> entities,double time) {
    TargetingVisual out;if(!s.active&&!s.preview_only)return out;
    out.area.reserve(s.area.size());
    for(const auto &cell:s.area) {
        auto q=colored(s);q.center={cell.point.x+.5f,cell.point.y+.5f,.035f};q.right={.48f,0.f,0.f};q.up={0.f,.48f,0.f};
        q.style=cell.possible?2.f:1.f;q.a=cell.possible?.11f:.16f;out.area.push_back(q);
    }
    constexpr double gap=.42,speed=2.4;
    const double phase=std::fmod(std::max(0.,time)*speed,gap);
    for(const auto &path:s.paths) {
        double next=phase,travel=0.;
        for(size_t i=1;i<path.points.size();++i) {
            const auto a=path.points[i-1],b=path.points[i];const double dx=b.x-a.x,dy=b.y-a.y,len=std::hypot(dx,dy);
            if(len==0.)continue;
            while(next<=travel+len) {
                const float t=float((next-travel)/len);auto q=colored(s);
                q.center={a.x+.5f+float(dx)*t,a.y+.5f+float(dy)*t,.12f};
                const auto normal=toward_eye(eye,q.center.x,q.center.y);q.right={-normal.y*.05f,normal.x*.05f,0.f};q.up={0.f,0.f,.05f};
                q.style=3.f;q.a=path.guide?.2f:(path.possible?.55f:.95f);
                if(path.guide&&path.points.size()==2&&std::max(std::abs(dx),std::abs(dy))<=1.)q.a*=float(std::max(0.,1.-next/len));
                out.dots.push_back(q);next+=gap;
            }
            travel+=len;
        }
    }
    for(const auto &marker:s.markers) {
        auto q=colored(s);q.center={marker.point.x+.5f,marker.point.y+.5f,.05f};
        q.right={.43f,0.f,0.f};q.up={0.f,.43f,0.f};
        q.a=!marker.valid?.25f:((marker.affected||s.kind=="neutral")?.95f:.28f);
        q.selected=marker.selected?1.f:0.f;q.dashed=marker.valid?0.f:1.f;
        const auto body=std::find_if(entities.begin(),entities.end(),[&](const auto &e){return marker.id?e.id==marker.id:(e.gx==marker.point.x&&e.gy==marker.point.y);});
        if(marker.creature&&(body!=entities.end()||marker.id!=-1)) {
            const float x=body==entities.end()?q.center.x:body->x,y=body==entities.end()?q.center.y:body->y;
            const float height=body==entities.end()?1.f:body->height,z=body==entities.end()?0.f:body->z;
            const auto normal=body==entities.end()?toward_eye(eye,x,y):body_normal(*body,eye);const float radius=std::clamp(height*.42f,.22f,.72f);
            q.center={x+normal.x*.10f,y+normal.y*.10f,z+height*.53f};q.right={-normal.y*radius,normal.x*radius,0.f};q.up={0.f,0.f,radius};
        }
        out.reticles.push_back(q);
    }
    return out;
}
bool pick_targeting_entity(Vec3 origin,Vec3 direction,std::span<const TargetingEntity> entities,TargetingPoint &point) {
    float best=std::numeric_limits<float>::infinity();bool found=false;
    for(const auto &e:entities) {
        if(e.id==-1)continue;
        const auto normal=body_normal(e,origin);const float denominator=dot(direction,normal);
        if(std::abs(denominator)<1e-5f)continue;
        const float t=dot(Vec3{e.x,e.y,e.z}-origin,normal)/denominator;if(t<=0.f||t>=best)continue;
        const auto at=origin+direction*t;const Vec3 right{-normal.y,normal.x,0.f};
        if(std::abs(dot(at-Vec3{e.x,e.y,e.z},right))>e.width*.5f||at.z<e.z||at.z>e.z+e.height)continue;
        best=t;point={e.gx,e.gy};found=true;
    }
    return found;
}
}
