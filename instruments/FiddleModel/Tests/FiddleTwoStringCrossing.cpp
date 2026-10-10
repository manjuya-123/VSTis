#include "Rebuild/TwoStringFiddleBridge.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
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
    std::array<std::vector<float>,2> stringVelocities,bowForceTraces;
    for(int j=0;j<2;++j){stringVelocities[j].reserve(total);bowForceTraces[j].reserve(total);}
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
        peakBridge=std::max(peakBridge,std::max(std::abs(model.mechanics().bridgePosition(0)),
                                   std::abs(model.mechanics().bridgePosition(1))));
        peakEnergy=std::max(peakEnergy,model.mechanics().energy());
        if(!std::isfinite(velocity)||!std::isfinite(peakEnergy)||
           peakEnergy>0.1||std::abs(velocity)>20.0||worstLedger>1e-8||
           worstRoot>1e-5||minHairDiss< -1e-8){ok=false;break;}
        const int segment=t<0.7?0:(t>1.1&&t<1.7?1:(t>2.2?2:-1));
        if(segment>=0){rmsStage[segment]+=velocity*velocity;++countStage[segment];}
        sound.push_back(float(velocity));
        const auto vs=model.stringBowVelocities();
        const auto fb=model.bowForces();
        for(int j=0;j<2;++j){
            stringVelocities[j].push_back(float(vs[j]));
            bowForceTraces[j].push_back(float(fb[j]));
        }
    }
    if(sound.size()!=total)ok=false;
    for(int j=0;j<3;++j)if(countStage[j]>0)
        rmsStage[j]=std::sqrt(rmsStage[j]/countStage[j]);

    const auto lockIn=[&](const std::vector<float>& samples,
                          double start,double stop,double hz){
        const int from=int(start*rate),end=int(stop*rate);
        double real=0,imag=0;
        for(int n=from;n<end;++n){
            const double angle=6.2831853071795864769*hz*double(n-from)/rate;
            real+=samples[std::size_t(n)]*std::cos(angle);
            imag+=samples[std::size_t(n)]*std::sin(angle);
        }
        return 2*std::hypot(real,imag)/(end-from);
    };
    const double bridgeE440=lockIn(sound,1.2,1.7,440.0);
    const double bridgeE659=lockIn(sound,1.2,1.7,659.2551138257);
    const double contactE440=lockIn(stringVelocities[1],1.2,1.7,440.0);
    const double contactE659=lockIn(stringVelocities[1],1.2,1.7,659.2551138257);
    const double forceE440=lockIn(bowForceTraces[1],1.2,1.7,440.0);
    const double forceE659=lockIn(bowForceTraces[1],1.2,1.7,659.2551138257);
    std::cout<<"E-bow stage spectral bridge A440="<<bridgeE440
             <<" E659="<<bridgeE659
             <<" ; E-string velocity A440="<<contactE440
             <<" E659="<<contactE659
             <<" ; E-bow force A440="<<forceE440
             <<" E659="<<forceE659<<"\\n";
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
bool independentEOnlyProbe(const char* path){
    TwoStringFiddleBridge m;if(!m.prepare(48000))return false;
    TwoStringFiddleBridge::Gesture g;g.crossing=1;g.bowSpeed=0.22;g.normalForce=0.30;
    constexpr int rate=48000,total=2*rate;
    std::vector<float> bridge;bridge.reserve(total);
    double eReal=0,eImag=0,aReal=0,aImag=0;
    double vEReal=0,vEImag=0;
    double eForces=0,peak=0;
    for(int i=0;i<total;++i){
        const double y=m.step(g);
        const auto v=m.stringBowVelocities();
        const auto F=m.bowForces();
        bridge.push_back(float(y));
        peak=std::max(peak,std::abs(y));
        if(i>=rate && i<2*rate){
            const double t=double(i-rate)/rate;
            const double phaseE=6.283185307179586*659.2551138257*t;
            const double phaseA=6.283185307179586*440.0*t;
            eReal+=y*std::cos(phaseE);
            eImag+=y*std::sin(phaseE);
            aReal+=y*std::cos(phaseA);
            aImag+=y*std::sin(phaseA);
            vEReal+=v[1]*std::cos(phaseE);
            vEImag+=v[1]*std::sin(phaseE);
            eForces+=std::abs(F[1]);
        }
    }
    std::cout<<"E-only after 1s: bridge E659="<<2*std::hypot(eReal,eImag)/rate
             <<" bridge A440="<<2*std::hypot(aReal,aImag)/rate
             <<" E-string bow contact E659="<<2*std::hypot(vEReal,vEImag)/rate
             <<" mean E bow force="<<eForces/rate<<"\n";
    if(path!=nullptr&&peak>0){
        for(auto& sample:bridge)sample=float(sample*0.6/peak);
        return writeWave(path,bridge);
    }
    return std::isfinite(eForces);
}
int main(int argc,char** argv){
    bool ok=runCrossing(argc>1?argv[1]:nullptr);
    if(argc>1){
        std::string path(argv[1]);
        const auto slash=path.find_last_of("/\\");
        if(slash!=std::string::npos)path=path.substr(0,slash+1);
        else path.clear();
        path+="two_string_E_only_bridge_velocity_NOT_violin.wav";
        ok=independentEOnlyProbe(path.c_str())&&ok;
    }else{
        ok=independentEOnlyProbe(nullptr)&&ok;
    }
    return ok?EXIT_SUCCESS:EXIT_FAILURE;
}
