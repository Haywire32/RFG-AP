#pragma once

namespace DistanceFog {
struct Toggle {
    bool visible=true;
    bool released=false;
    bool Poll(bool down,bool active,bool modified) {
        if(!active || modified) {released=false;return false;}
        if(!down) {released=true;return false;}
        if(!released) return false;
        released=false;visible=!visible;return true;
    }
};
}
