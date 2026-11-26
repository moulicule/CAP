#include "../src/companion_logic.h"
#include "../src/glasses_database.h"
#include <cassert>
#include <initializer_list>
#include <cstdio>
#include <limits>

static const uint8_t A[7] = {1}, B[7] = {2};

void testMatching() {
    const uint8_t nothing[] = {0xCB,0x0C}, incorrect[] = {0xCA,0x0C}, google[] = {0xE0,0};
    assert(matchDevice("Ear (2)", nothing, 2, true).testDevice);
    assert(!matchDevice("Ear (2)", nothing, 2, false).matched);
    assert(strstr(matchDevice("Nothing Ear (2)", nothing, 2, true).reason, "0CCB"));
    assert(!matchDevice("", incorrect, 2, true).matched);
    assert(!matchDevice("", google, 2, true).matched);
    assert(!matchDevice("G1 Speaker", nullptr, 0, true).matched);
    assert(!matchDevice("Bedtime Stories", nullptr, 0, true).matched);
    assert(!matchDevice("xrealistic speaker", nullptr, 0, true).matched);
    assert(!matchDevice("monocle", nullptr, 0, true).matched);
    assert(matchDevice("Ray-Ban Meta", nullptr, 0, false).matched);
    assert(matchDevice("XREAL Air", nullptr, 0, false).matched);
    assert(matchDevice("Razer Anzu", nullptr, 0, false).matched);
    assert(matchDevice("Brilliant Frame", nullptr, 0, false).matched);
    assert(!matchDevice("Google Speaker", google, 1, false).matched);
}

void testTracking() {
    DetectionTracker tracker;
    tracker.observe(A,-45,"near","name",false,0);
    assert(tracker.strongest(0).valid); // A packet at timestamp zero is valid.
    tracker.observe(B,-85,"far","name",false,100);
    auto sample = tracker.strongest(100);
    assert(sample.rssi == -45 && !strcmp(sample.name,"near"));
    uint32_t sequence = sample.sequence;
    tracker.observe(B,-90,"far","name",false,1000);
    assert(tracker.strongest(1000).sequence == sequence);
    assert(!strcmp(tracker.strongest(1500).name,"far"));
    assert(!tracker.strongest(2500).valid);
    tracker.observe(A,-90,"reappeared","name",false,3000);
    assert(tracker.strongest(3000).rssi == -90); // No filter history after expiry.
    tracker = {};
    tracker.observe(A,-50,"wrap","name",true,0xFFFFFF00u);
    assert(tracker.strongest(0x20).valid);
    assert(!tracker.strongest(0x600).valid);
    tracker.removeTestDevices();
    assert(!tracker.strongest(0x20).valid);
    tracker = {};
    tracker.observe(A,127,"invalid","",false,0);
    assert(!tracker.strongest(0).valid);
    for (unsigned i=0;i<9;++i) {
        uint8_t key[7] = {(uint8_t)i};
        tracker.observe(key,-80,"bounded","",false,i);
    }
    unsigned count=0;
    for (const auto& device:tracker.devices) { count+=device.sample.valid; assert(device.sample.address[0]!=0); }
    assert(count==8);
    tracker={};
    for (uint32_t t=0;t<=2400;t+=100) {
        tracker.observe(A,t%200==0?-49:-50,"A","",false,t);
        tracker.observe(B,t%200==0?-50:-49,"B","",false,t);
    }
    for (const auto& device:tracker.devices) {
        if (device.sample.valid) assert(device.sample.tier==PROX_SUPER_CLOSE);
    }
    assert(tracker.strongest(2400).tier==PROX_SUPER_CLOSE);
    int readings[] = {-90,-70,-50};
    do {
        tracker={};
        for(unsigned i=0;i<3;i++) tracker.observe(A,readings[i],"median","",false,i*100);
        assert(tracker.strongest(200).rssi==-70);
    } while(std::next_permutation(readings,readings+3));
}

void testProximity() {
    DetectionTracker tracker;
    // Restore the pre-audit behaviour: classify the first packet immediately.
    tracker.observe(A,-45,"near","",false,0);
    assert(tracker.strongest(0).tier==PROX_SUPER_CLOSE);
    assert(!tracker.strongest(1500).valid);

    tracker={};
    tracker.observe(A,-82,"far","",false,100);
    tracker.observe(A,-45,"spike","",false,200);
    for (uint32_t t=200;t<300;t+=10) assert(tracker.strongest(t).tier==PROX_DISTANT);
    tracker.observe(A,-82,"normal","",false,300);
    assert(tracker.strongest(300).tier==PROX_DISTANT);

    // A real move reacts on the second fresh packet, with no one-second hold.
    tracker.observe(A,-45,"near","",false,400);
    tracker.observe(A,-45,"near","",false,450);
    assert(tracker.strongest(450).tier==PROX_SUPER_CLOSE);
    tracker.observe(A,-90,"far","",false,500);
    tracker.observe(A,-90,"far","",false,550);
    assert(tracker.strongest(550).tier==PROX_DISTANT);
    tracker.observe(A,-68,"near","",false,600);
    tracker.observe(A,-68,"near","",false,650);
    assert(tracker.strongest(650).tier==PROX_NEAR);

    ProximityState proximity;
    proximity.observe(-70);assert(proximity.tier==PROX_NEAR);
    proximity.observe(-75);assert(proximity.tier==PROX_NEAR);
    proximity.observe(-79);assert(proximity.tier==PROX_DISTANT);
    proximity.observe(-54);assert(proximity.tier==PROX_SUPER_CLOSE);
    proximity.observe(-60);assert(proximity.tier==PROX_SUPER_CLOSE);
    proximity.observe(-63);assert(proximity.tier==PROX_NEAR);

    tracker={};
    tracker.observe(A,-45,"A","",false,0xFFFFFF00u);
    tracker.observe(A,-90,"A","",false,0xFFFFFFF0u);
    tracker.observe(A,-90,"A","",false,40);
    assert(tracker.strongest(40).tier==PROX_DISTANT);
    tracker.observe(B,-45,"B","",false,50);
    assert(tracker.strongest(50).tier==PROX_SUPER_CLOSE);
    // The second device does not change the first device's filter or tier.
    for (const auto& device:tracker.devices)
        if (device.sample.valid && device.sample.address[0]==A[0]) assert(device.sample.tier==PROX_DISTANT);
}

void testHapticsAndTime() {
    HapticSequence h;
    h.begin(HAPTIC_SINGLE,0xFFFFFFC0u);
    h.advance(0xFFFFFFC0u+119);assert(h.output);
    h.advance(0xFFFFFFC0u+120);assert(!h.output && h.pattern==HAPTIC_NONE);
    h.begin(HAPTIC_DOUBLE,0);
    h.advance(74);assert(h.output);
    h.advance(75);assert(!h.output);
    h.advance(140);assert(h.output);
    h.advance(215);assert(!h.output);
    h.begin(HAPTIC_RAGE,0);
    const uint32_t times[]={0,60,100,160,200,290};
    for(unsigned i=0;i<6;i++){h.advance(times[i]);assert(h.output==(i<5 && i%2==0));}
    h.begin(HAPTIC_RAGE,0);h.advance(1000);assert(!h.output);
    h.begin(HAPTIC_SINGLE,100);h.begin(HAPTIC_NONE,110);assert(!h.output);
    assert(!deadlineReached(0xFFFFFFF0u,20));
    assert(deadlineReached(21,20));
    assert((uint32_t)(100u-0xFFFFFFF0u)==116);
    IdleClock idle;
    idle.activity(0xFFFFFFF0u);
    assert(idle.tick(100)==116);
    assert(idle.tick(100000)==SLEEP_DEEP_TIMEOUT_MS);
    assert(idle.tick(0xFFFFFFF0u)==SLEEP_DEEP_TIMEOUT_MS);
    assert(idle.tick(100)==SLEEP_DEEP_TIMEOUT_MS); // Never wake after 49 days of idle.
    idle.activity(101);assert(idle.tick(102)==1);
}

void testColorsAndCommands() {
    const uint16_t colors[]={0,0xFFFF,0xF800,colorRGB(186,54,54),colorRGB(97,0,0)};
    for (uint16_t from:colors) for(uint16_t to:colors) {
        uint16_t value=from;
        for(int i=0;i<1000;i++)value=lerpColor565(value,to,0.1f);
        assert(value==to);
        assert(lerpColor565(from,to,0)==from);
        assert(lerpColor565(from,to,1.5f)==to);
    }
    CommandBuffer line;
    for(char c: {'r','a','g','e'})assert(!line.push(c));
    assert(line.push('\n') && !strcmp(line.text,"rage"));
    for(int i=0;i<70;i++)line.push('x');
    assert(!line.push('\n'));
    for(char c: {'p','e','r','f','\r'})assert(!line.push(c));
    assert(line.push('\n') && !strcmp(line.text,"perf"));
}

int main() {
    testMatching();testTracking();testProximity();testHapticsAndTime();testColorsAndCommands();
    puts("PASS: matching, multi-device tracking, spikes, expiry, hysteresis, rollover, haptics, color convergence, serial input");
}
