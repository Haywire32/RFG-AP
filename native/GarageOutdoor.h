#pragma once
#include <cstdint>
#include <cstring>
#include <initializer_list>
namespace GarageOutdoor {
struct Point {uint32_t node;float position[3],orientation[9];};
// User-recorded outdoor spawn centres and facing, 2026-10-01.
inline constexpr Point Points[]={
    {0x47020041,{-1695.458374f,38.877586f,445.476532f},{-0.248913f,0.000000f,-0.968526f,0.000000f,1.000000f,0.000000f,0.968526f,0.000000f,-0.248913f}},
    {0x4a0200dc,{-1886.855347f,15.002240f,-1436.734009f},{0.481722f,0.000000f,-0.876324f,-0.000000f,1.000000f,0.000000f,0.876324f,0.000000f,0.481722f}},
    {0x56020041,{-1360.242920f,23.969158f,557.997131f},{0.242416f,0.000000f,-0.970172f,-0.000000f,1.000000f,0.000000f,0.970172f,0.000000f,0.242416f}},
    {0x7904000c,{-379.903320f,32.701912f,-816.471436f},{-0.667321f,0.000000f,-0.744770f,0.000000f,1.000000f,0.000000f,0.744770f,0.000000f,-0.667321f}},
    {0x7c010041,{-108.513206f,23.472113f,-2445.695068f},{0.188880f,-0.000000f,0.982000f,0.000000f,1.000000f,0.000000f,-0.982000f,0.000000f,0.188880f}},
    {0xa604007e,{1433.741333f,2.598152f,663.924072f},{0.520622f,0.000000f,-0.853788f,-0.000000f,1.000000f,0.000000f,0.853788f,0.000000f,0.520622f}},
    {0xc80201c0,{2388.244873f,43.421059f,-294.193115f},{0.323619f,-0.000000f,0.946188f,0.000000f,1.000000f,0.000000f,-0.946188f,0.000000f,0.323619f}},
    {0xca010007,{2461.936035f,50.530506f,-1233.068970f},{-0.972106f,0.000000f,0.234542f,0.000000f,1.000000f,0.000000f,-0.234542f,0.000000f,-0.972106f}},
};
inline const Point* Find(uint32_t node) {for(const auto& p:Points) if(p.node==node) return &p;return nullptr;}
inline bool Family(const char* name) {
    for(const char* family:{"EDF APC","Heavy tank","Medium tank","Artillery tank","Heavy walker","Light walker","Combat walker","Bus","Flatbed","Supply truck","EDF supply","Garbage truck","Dump truck","Fuel tanker"})
        if(std::strcmp(name,family)==0) return true;
    return false;
}
}
