"""Exercise the real sensor setup/recovery code with injected I2C failures."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'src/main.cpp').read_text()
registers = source[source.index('#define ADXL345_ADDR'):source.index('bool g_adxlAvailable')]
sensor = source[source.index('bool g_adxlAvailable'):source.index('// -------------------------------------------------------------\n// Vibration Motor')]
service = source[source.index('void serviceADXL345('):source.index('void beginSimulation(')]
harness = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#define IRAM_ATTR
#define INPUT 0
#define RISING 1
#define portMUX_TYPE int
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
#define portENTER_CRITICAL_ISR(x) ((void)0)
#define portEXIT_CRITICAL_ISR(x) ((void)0)
uint32_t clockMs=100;
uint32_t millis(){return clockMs;}
bool irq=false,settling=false;
int taps=0;
int digitalPinToInterrupt(int pin){return pin;}
void pinMode(int,int){}
void attachInterrupt(int,void(*)(),int){irq=true;}
void detachInterrupt(int){irq=false;}
bool motorIsSettling(uint32_t){return settling;}
void triggerDoubleTap(const char*){taps++;}
struct SerialStub {void println(const char*){}} Serial;
struct WireStub {
    uint8_t data[256]={};
    uint8_t reg=0,value=0;
    int writes=0,corrupt=-1;
    bool nack=false,shortRead=false;
    void beginTransmission(uint8_t){writes=0;}
    void write(uint8_t byte){if(writes++==0)reg=byte;else value=byte;}
    int endTransmission(bool=true){if(nack)return 2;if(writes==2)data[reg]=value;return 0;}
    int requestFrom(uint8_t,uint8_t){return shortRead?0:1;}
    int read(){int value=data[reg];if(reg==0x30)data[reg]=0;return reg==corrupt?value^1:value;}
} Wire;
'''
tests = r'''
void reset(){Wire={};Wire.data[0]=0xE5;clockMs=100;g_sensorAttempts=0;g_adxlAvailable=false;irq=false;settling=false;taps=0;}
int main(){
    reset();assert(initADXL345());assert(irq && g_adxlAvailable);
    assert(Wire.data[0x2D]==8 && Wire.data[0x2E]==0x20 && Wire.data[0x31]==0x0B);
    reset();Wire.data[0]=0;assert(!initADXL345());assert(!irq && !g_adxlAvailable);
    reset();Wire.nack=true;assert(!initADXL345());
    reset();Wire.shortRead=true;assert(!initADXL345());
    reset();Wire.corrupt=0x1D;assert(!initADXL345());assert(!g_adxlAvailable);
    reset();Wire.nack=true;initADXL345();
    for(int i=1;i<20;i++){clockMs=100+i*5000;serviceADXL345(clockMs);}
    assert(g_sensorAttempts==3); // Automatic retries are bounded.
    reset();assert(initADXL345());Wire.shortRead=true;clockMs+=5000;serviceADXL345(clockMs);
    assert(!g_adxlAvailable && !irq);
    Wire.shortRead=false;clockMs+=5000;serviceADXL345(clockMs);assert(g_adxlAvailable);
    Wire.data[0x30]=0x20;g_doubleTapDetected=true;settling=true;serviceADXL345(clockMs);
    assert(taps==0 && Wire.data[0x30]==0);
    Wire.data[0x30]=0x20;g_doubleTapDetected=true;settling=false;serviceADXL345(clockMs);
    assert(taps==1);
    puts("PASS: sensor ID, ACK errors, short reads, register mismatch, bounded retries, recovery, motor-noise suppression");
}
'''
with tempfile.TemporaryDirectory(prefix='screen-sensor-test-') as tmp:
    cpp = Path(tmp) / 'sensor.cpp'
    cpp.write_text(harness + registers + sensor + service + tests)
    binary = Path(tmp) / 'sensor'
    subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-fsanitize=undefined', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
