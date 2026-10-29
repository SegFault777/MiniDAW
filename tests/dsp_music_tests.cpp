#include "../src/dsp/Drummer.h"
#include "../src/dsp/MidiFile.h"
#include "../src/dsp/DrumKit.h"
#include <cstdio>
#include <cmath>
int fails=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); ++fails; } }while(0)
int main(){
  for(int s=0;s<dsp::drummerStyleCount();++s){
    dsp::DrummerParams p; p.style=s; p.bars=4; p.complexity=0.8; p.loudness=0.9;
    auto h=dsp::generateDrummer(p,4,4);
    CHECK(h.size()>20); for(auto& x: h){ CHECK(x.beat>=0 && x.beat<16.1); CHECK(x.velocity>=1&&x.velocity<=127);} 
    printf("%s: %zu hits\n", dsp::drummerStyleName(s), h.size());
  }
  dsp::DrummerParams q; q.bars=2; auto a=dsp::generateDrummer(q,4,4); q.seed=7; auto b=dsp::generateDrummer(q,4,4);
  CHECK(a.size()>0); auto h34=dsp::generateDrummer(q,3,4); CHECK(h34.size()>0); for(auto&x:h34) CHECK(x.beat<6.1);
  auto h68=dsp::generateDrummer(q,6,8); CHECK(h68.size()>0);
  dsp::MidiSong s; s.bpm=133; s.num=6; s.den=8; dsp::MidiTrack t; t.name="Lead";
  t.notes.push_back({0,60,100,0,1}); t.notes.push_back({0,64,90,1,0.5}); t.notes.push_back({9,36,110,2.25,0.25}); s.tracks.push_back(t);
  auto bytes=dsp::writeMidiFile(s); dsp::MidiSong r; std::string err;
  CHECK(dsp::readMidiFile(bytes,r,err)); CHECK(std::fabs(r.bpm-133)<0.01); CHECK(r.num==6&&r.den==8); CHECK(r.tracks.size()==1&&r.tracks[0].notes.size()==3);
  CHECK(r.tracks[0].name=="Lead"); CHECK(std::fabs(r.tracks[0].notes[2].startBeat-2.25)<1e-3 && r.tracks[0].notes[2].channel==9);
  std::vector<std::uint8_t> junk{1,2,3}; CHECK(!dsp::readMidiFile(junk,r,err));
  dsp::DrumKit k; float buf[2*512]={}; k.noteOn(36,100); k.render(buf,512); float pk=0; for(float v:buf) pk=std::max(pk,std::fabs(v)); CHECK(pk>0.1f);
  printf("fails=%d\n",fails); return fails;
}
