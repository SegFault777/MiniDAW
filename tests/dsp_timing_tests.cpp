#include "../src/dsp/TempoMap.h"
#include "../src/dsp/Stretch.h"
#include "../src/dsp/Synth.h"
#include <cstdio>
#include <cmath>
int fails=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); ++fails; } }while(0)
int main(){
  dsp::TempoMap t;
  CHECK(std::fabs(t.beatToFrame(4)-4*60.0*44100/120)<1e-6);
  t.setTempo({{0,120,true},{8,60,false},{16,90,false}});
  for(double b: {0.0,1.5,4.0,7.99,8.0,12.0,16.0,30.0}){ double f=t.beatToFrame(b); CHECK(std::fabs(t.frameToBeat(f)-b)<1e-6); }
  CHECK(std::fabs(t.bpmAt(4)-90)<1e-9);          // halfway through the 120->60 ramp
  CHECK(t.beatToFrame(8) > 8*60.0*44100/120);   // ramp to a slower tempo takes longer than constant 120
  t.setMeter({{1,4,4},{3,6,8},{5,3,4}});
  CHECK(std::fabs(t.barLength(3)-3.0)<1e-9);
  CHECK(std::fabs(t.barStart(3)-8.0)<1e-9);   // two 4/4 bars
  CHECK(std::fabs(t.barStart(5)-14.0)<1e-9);  // plus two 6/8 bars of 3 beats
  CHECK(t.barAt(9.0)==3 && t.barAt(11.0)==4 && t.barAt(14.0)==5);
  CHECK(std::fabs(t.nextBarLine(9.0)-11.0)<1e-9 && std::fabs(t.nextBarLine(11.0)-11.0)<1e-9);
  int clicks=0, accents=0; t.forEachClick(0,14,[&](double,bool a){++clicks; if(a)++accents;});
  CHECK(clicks==8+12); CHECK(accents==4);   // 2 bars of 4 quarter clicks + 2 bars of 6 eighth clicks
  // stretch: 440 Hz sine, 2 s
  std::vector<float> s(88200*2); for(size_t i=0;i<88200;++i){ float v=0.5f*std::sin(2*3.14159265*440*i/44100.0); s[2*i]=s[2*i+1]=v; }
  auto zc=[&](const std::vector<float>& x){ int z=0; for(size_t i=1;i<x.size()/2;++i) if((x[2*(i-1)]<0)!=(x[2*i]<0)) ++z; return double(z)/(x.size()/2/44100.0)/2; };
  auto o=dsp::timeStretch(s.data(),88200,1.5);
  CHECK(std::fabs(double(o.size()/2)-132300)<2); CHECK(std::fabs(zc(o)-440)<8);
  auto o2=dsp::timeStretch(s.data(),88200,0.7); CHECK(std::fabs(zc(o2)-440)<8);
  auto f=dsp::flex(s.data(),88200,1.0,12); CHECK(std::fabs(double(f.size()/2)-88200)<400); CHECK(std::fabs(zc(f)-880)<20);
  float pk=0; for(size_t i=4000;i<o.size()-4000;++i) pk=std::max(pk,std::fabs(o[i])); CHECK(pk>0.4f && pk<0.62f);
  printf("fails=%d stretchpeak=%.3f\n",fails,pk); return fails;
}
