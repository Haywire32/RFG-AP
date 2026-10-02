#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
namespace GunshipPolicy {
constexpr uint32_t None=0xffffffffu;
constexpr float Pi=3.14159265359f;
struct Vec {float x=0,y=0,z=0;};
inline Vec operator+(Vec a,Vec b) {return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec operator-(Vec a,Vec b) {return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec operator*(Vec a,float s) {return {a.x*s,a.y*s,a.z*s};}
inline float Length(Vec a) {return std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);}
inline bool Finite(Vec a) {return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
inline Vec Unit(Vec a) {const float n=Length(a);return n>0.0001f && std::isfinite(n)?a*(1/n):Vec{};}
inline float Wrap(float a) {return std::isfinite(a)?std::remainder(a,2*Pi):0;}
inline Vec Forward(float yaw,float pitch=0) {return {std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};}
inline float Dot(Vec a,Vec b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec Cross(Vec a,Vec b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline Vec PilotAngularVelocity(Vec right,Vec up,Vec forward,Vec angular,float yaw,Vec travel,float dt) {
    if(!Finite(right)||!Finite(up)||!Finite(forward)||!Finite(angular)||!Finite(travel)||
        !std::isfinite(yaw)||!std::isfinite(dt)||dt<=0) return angular;
    // A fixed heading and a small travel lean replace the AI's acceleration-
    // dependent tilt target. Angular damping prevents alternating corrections.
    const Vec heading=Forward(yaw),side{heading.z,0,-heading.x};
    const float pitch=-.08f*std::clamp(Dot(travel,heading)/50.4f,-1.0f,1.0f);
    const float bank=.08f*std::clamp(Dot(travel,side)/28.0f,-1.0f,1.0f);
    const Vec targetForward=Forward(yaw,pitch);
    const Vec targetUp=Unit(Cross(targetForward,side)+side*bank);
    const Vec targetRight=Unit(Cross(targetUp,targetForward));
    const Vec error=(Cross(right,targetRight)+Cross(up,targetUp)+Cross(forward,targetForward))*.5f;
    Vec desired=error*2.0f;
    desired.y=std::clamp(desired.y,-.8f,.8f);
    const float tilt=std::sqrt(desired.x*desired.x+desired.z*desired.z);
    if(tilt>.45f) {desired.x*=.45f/tilt;desired.z*=.45f/tilt;}
    return angular+(desired-angular)*(1-std::exp(-6.0f*std::min(dt,.1f)));
}
inline bool Supported(const char* name) {
    return name && (!std::strcmp(name,"EDF_AAXAir_1") || !std::strcmp(name,"EDF_AAXAir_2") || !std::strcmp(name,"EDF_AAXAir_Gauss"));
}
struct Input {float forward=0,side=0,vertical=0,mouseX=0,mouseY=0,sensitivity=0.0025f;bool enabled=false;};
struct ExitInput {
    bool released=false,held=true,pressed=false;
    void Reset() {released=false;held=true;pressed=false;}
    bool Sample(bool down,bool enabled) {
        // Boarding and focus/menu transitions must see a real key release.
        pressed=false;
        if(!enabled) {Reset();return false;}
        if(!down) {released=true;held=false;return false;}
        pressed=released&&!held;held=true;return pressed;
    }
};
struct AimInput {
    ExitInput edge;
    bool active=false;
    void Reset() {edge.Reset();active=false;}
    bool Sample(bool down,bool enabled,bool holdToAim) {
        const bool pressed=edge.Sample(down,enabled);
        if(!enabled) active=false;
        else if(holdToAim) active=down&&edge.released;
        else if(pressed) active=!active;
        return active;
    }
};
struct AimZoom {
    float amount=0;
    void Reset() {amount=0;}
    float Step(bool aim,float dt) {
        if(std::isfinite(dt)&&dt>0) {
            const float step=std::min(dt,.1f)/.18f;
            amount=aim?std::min(1.0f,amount+step):std::max(0.0f,amount-step);
        }
        return 1.0f-.35f*(amount*amount*(3-2*amount));
    }
};
struct Motion {
    float yaw=0,pitch=0,cruiseTime=0;Vec velocity{},anchor{};bool holding=true;
    void Reset(Vec pos,float heading) {yaw=Wrap(heading);pitch=0;cruiseTime=0;velocity={};anchor=pos;holding=true;}
    void Step(Input input,Vec position,float heading,float dt) {
        if(!Finite(position) || !std::isfinite(heading) || !std::isfinite(dt) || dt<=0) return;
        dt=std::min(dt,0.05f);
        if(!input.enabled) input={};
        if(std::isfinite(input.mouseX) && std::isfinite(input.mouseY) && std::isfinite(input.sensitivity)) {
            const float scale=std::clamp(input.sensitivity,0.0001f,0.015f);
            // Bound a single input sample and keep the camera within the turret arc.
            yaw=Wrap(yaw+std::clamp(input.mouseX*scale,-0.3f,0.3f));
            if(input.mouseX!=0) yaw=Wrap(heading+std::clamp(Wrap(yaw-heading),-0.9f,0.9f));
            pitch=std::clamp(pitch-input.mouseY*scale,-0.65f,0.32f);
        }
        Vec local{input.side,0,input.forward};
        if(!Finite(local)) local={};
        if(Length(local)>1) local=Unit(local);
        const Vec forward=Forward(yaw),right{forward.z,0,-forward.x};
        // Short taps retain the familiar maneuvering speed; sustained forward
        // flight smoothly reaches the faster travel speed after two seconds.
        cruiseTime=input.forward>.5f?std::min(2.0f,cruiseTime+dt):0;
        const float forwardSpeed=28.0f+22.4f*std::clamp((cruiseTime-.5f)/1.5f,0.0f,1.0f);
        Vec target=forward*(local.z*forwardSpeed)+right*(local.x*28.0f);
        target.y=std::isfinite(input.vertical)?std::clamp(input.vertical,-1.0f,1.0f)*12.0f:0;
        // Frame-rate independent input easing; native physics adds inertia/banking.
        const float response=1-std::exp(-dt*5.0f);
        velocity=velocity+(target-velocity)*response;
        if(Length(target)<0.01f && Length(velocity)<0.2f) {
            velocity={};if(!holding) anchor=position;holding=true;
        } else {holding=false;anchor=position;}
    }
};
inline bool SafeParking(float clearance,float speed,float upright) {
    return std::isfinite(clearance)&&clearance>=-1.0f&&clearance<1.0f &&
        std::isfinite(speed)&&speed<2.5f && std::isfinite(upright)&&upright>0.65f;
}
inline float DescentScale(float clearance) {
    return std::isfinite(clearance)?std::clamp((clearance+0.5f)/12.0f,0.06f,1.0f):1.0f;
}
inline float EntryDistance(Vec human,Vec craft,const float* basis) {
    if(!Finite(human)||!Finite(craft)||!basis) return INFINITY;
    float closest=INFINITY;
    // Use the native DrStrt boarding height, with access from either side.
    // Boarding is relative to the person at the door, not the terrain below a pad.
    for(float side:{-2.418f,2.418f}) {
        const Vec entry=craft+Vec{basis[0],basis[1],basis[2]}*side+
            Vec{basis[3],basis[4],basis[5]}*-1.31094f+Vec{basis[6],basis[7],basis[8]}*.560116f;
        const Vec d=human-entry;
        if(Finite(d)&&std::abs(d.y)<2.5f && d.x*d.x+d.z*d.z<16.0f)
            closest=std::min(closest,d.x*d.x+d.y*d.y+d.z*d.z);
    }
    return closest;
}
inline bool EntryRange(Vec human,Vec craft,const float* basis) {
    return std::isfinite(EntryDistance(human,craft,basis));
}
}
