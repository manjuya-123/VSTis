#include "Rebuild/TwoStringFiddleBridge.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

using vstis::fiddle::rebuild::TwoStringFiddleBridge;

bool writeWave(const char* path,const std::vector<float>& s) {
    if(path==nullptr)return true;
    std::ofstream out(path,std::ios::binary);if(!out)return false;
    const std::uint32_t bytes=std::uint32_t(s.size()*sizeof(float));
    const auto u16=[&](std::uint16_t n) {
        out.write(reinterpret_cast<const char*>(&n),2);
    };
    const auto u32=[&](std::uint32_t n) {
        out.write(reinterpret_cast<const char*>(&n),4);
    };
    out.write("RIFF",4);u32(36+bytes);
    out.write("WAVEfmt ",8);u32(16);u16(3);u16(1);
    u32(48000);u32(48000*4);u16(4);u16(32);
    out.write("data",4);u32(bytes);
    out.write(reinterpret_cast<const char*>(s.data()),bytes);
    return out.good();
}
bool runCrossing(const char* path){
    constexpr int rate=48000,total=rate*3;
    TwoStringFiddleBridge model;
    if(!model.prepare(rate))return false;
    TwoStringFiddleBridge::Gesture gesture;
    gesture.bowSpeed=0.22;gesture.normalForce=0.30;
    std::vector<float> sound;sound.reserve(total);
    double worstLedger=0,worstRoot=0,minHairDiss=0;
    double peak=0,peakBridge=0,peakEnergy=0;
    double aNormalSum=0,eNormalSum=0;
    std::array<double,3> rmsStage{};
    std::array<int,3> countStage{};
    bool ok=true;
    for(int n=0;n<total;++n) {
        const double t=double(n)/rate;
        // A (0..0.75), continuous A->E (0.75..1),
        // E (1..1.8), continuous E->A (1.8..2.05), A to end.
        if(t<0.75)gesture.crossing=0;
        else if(t<1.0)gesture.crossing=(t-0.75)/0.25;
        else if(t<1.8)gesture.crossing=1;
        else if(t<2.05)gesture.crossing=1-(t-1.8)/0.25;
        else gesture.crossing=0;
        // No note trigger; no finger changes for this FIRST crossover test.
        // Test crossing acoustics before asking MIDI/Play Mode to drive it.
        const double velocity=model.step(gesture);
        worstLedger=std::max(worstLedger,std::abs(model.energyResidual()));
        worstRoot=std::max(worstRoot,model.rootResidual());
        minHairDiss=std::min(minHairDiss,model.hairDissipation());
        peak=std::max(peak,std::abs(velocity));
        peakBridge=std::max(peakBridge,std::abs(model.mechanics().bridgePosition()));
        peakEnergy=std::max(peakEnergy,model.mechanics().energy());
        if(!std::isfinite(velocity)||!std::isfinite(peakEnergy)||
           peakEnergy>0.1||std::abs(velocity)>20.0||worstLedger>1e-8||
           worstRoot>1e-5||minHairDiss< -1e-8){ok=false;break;}
        const int segment=t<0.7?0:(t>1.1&&t<1.7?1:(t>2.2?2:-1));
        if(segment>=0){rmsStage[segment]+=velocity*velocity;++countStage[segment];}
        sound.push_back(float(velocity));
    }
    if(sound.size()!=total)ok=false;
    for(int j=0;j<3;++j)if(countStage[j]>0)
        rmsStage[j]=std::sqrt(rmsStage[j]/countStage[j]);
    std::cout<<"physical A-E-A crossover: max energy residual "<<worstLedger
        <<" J; root residual "<<worstRoot
        <<" N; minimum hair dissipation "<<minHairDiss
        <<" J; maximum bridge position "<<peakBridge
        <<" m; bridge velocity peak "<<peak
        <<"; A/E/A stage RMS "
        <<rmsStage[0]<<", "<<rmsStage[1]<<", "<<rmsStage[2]<<"\n";
    if(!(rmsStage[0]>1e-10&&rmsStage[1]>1e-10&&rmsStage[2]>1e-10
         &&peakBridge>1e-12&&worstLedger<1e-10
         &&worstRoot<1e-7&&minHairDiss>=-1e-12))ok=false;
    if(ok&&peak>0.0){
        // Listening aid only. Uniform offline normalization is explicitly not
        // an instrument's physical radiation, loudness or dynamics model.
        const double gain=0.6/peak;
        for(auto& s:sound)s=float(s*gain);
        ok=writeWave(path,sound);
    }
    std::cout<<(ok?"TWO-STRING CROSSING NUMERICS PASS\n":
                    "TWO-STRING CROSSING NUMERICS FAIL\n");
    return ok;
}
int main(int argc,char** argv){
    return runCrossing(argc>1?argv[1]:nullptr)?EXIT_SUCCESS:EXIT_FAILURE;
}
