#include "../src/eye_renderer.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <initializer_list>

constexpr int W=240,H=240;

void checkPose(float w,float h,float r,float angle,float cx,float cy) {
    std::vector<uint16_t> guarded(W*H+2,0);
    guarded.front()=0x1234;guarded.back()=0x5678;
    auto pixels=guarded.data()+1;
    drawRoundedEye(pixels,W,H,cx,cy,w,h,r,angle,0xffff);
    assert(guarded.front()==0x1234 && guarded.back()==0x5678);
    double area=0;
    unsigned intermediate=0;
    for(int i=0;i<W*H;i++) {
        uint16_t color=eyeSwap565(pixels[i]);
        area+=((color>>5)&63)/63.0;
        intermediate+=color!=0 && color!=0xffff;
    }
    if(cx==120 && cy==120) {
        double radius=std::min<double>(r,std::min(w,h)*0.5);
        double expected=w*h-(4-3.141592653589793)*radius*radius;
        assert(fabs(area-expected)<std::max(3.0,expected*0.04));
        assert(intermediate>0);
    }
}

// Independent 16x16 coverage reference, tested at boundary pixels of a tilted capsule.
void checkCoverage() {
    std::vector<uint16_t> pixels(W*H,0);
    double theta=0.5235987755982988,c=cos(theta),s=sin(theta);
    drawRoundedEye(pixels.data(),W,H,120.25f,119.75f,32,64,16,30,0xffff);
    for(int y=75;y<165;y+=2) for(int x=75;x<165;x+=2) {
        int inside=0;
        for(int sy=0;sy<16;sy++)for(int sx=0;sx<16;sx++) {
            double dx=x+(sx+0.5)/16.0-120.25,dy=y+(sy+0.5)/16.0-119.75;
            double u=c*dx+s*dy,v=-s*dx+c*dy;
            double end=std::max(fabs(v)-16,0.0);
            if(u*u+end*end<=16*16)inside++;
        }
        double actual=((eyeSwap565(pixels[y*W+x])>>5)&63)/63.0;
        assert(fabs(actual-inside/256.0)<0.20);
    }
}

int main() {
    unsigned count=0;
    for(float angle : {-90.f,-55.f,-40.f,-5.f,0.f,30.f,45.f,90.f})
        for(float center : {-40.f,0.f,120.f,239.75f,280.f}) {
            checkPose(32,64,16,angle,center,center);
            checkPose(64,8,4,angle,center,center);
            checkPose(38,42,16,angle,center,center);
            checkPose(3,3,1.5f,angle,center,center);
            count+=4;
        }
    checkCoverage();
    std::vector<uint16_t> pixels(W*H,eyeSwap565(0xF800));
    drawRoundedEye(pixels.data(),W,H,120,120,32,64,16,0,0x07E0);
    assert(eyeSwap565(pixels[120*W+120])==0x07E0);
    assert(eyeSwap565(pixels[0])==0xF800);
    puts("PASS: rounded-eye area, rotation, subpixel edge coverage, off-screen clipping, RGB565 byte order and background preservation");
    printf("Checked %u poses and a 16x16 supersampling reference.\n",count);
}
