#include "physical/ImplicitString1D.h"
#include "Rebuild/BowedStringPilot.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

using vstis::physical::ImplicitString1D;
using vstis::fiddle::rebuild::BowedStringPilot;
using vstis::fiddle::rebuild::Gesture;

bool freeEnergyTest(double rate,double hz) {
    ImplicitString1D s;
    const double L=0.328,T=48.0;
    if(!s.prepare(rate,L,T,T/std::pow(2*L*hz,2),0.0))return false;
    s.seedStandingWaveForTest(0.000025);
    const auto b=s.at(0.0328),f=s.at(0.25);
    const double initial=s.energy();
    double maxDrift=0;
    for(int i=0;i<int(rate*0.2);++i){
        s.advance(s.begin(b,f),0,0);
        maxDrift=std::max(maxDrift,std::abs(s.energy()-initial)/initial);
    }
    std::cout<<"free vibration "<<hz<<" Hz @ "<<rate
             <<" relative energy drift "<<maxDrift<<"\n";
    return maxDrift<5.0e-8;
}

bool workIdentityTest() {
    ImplicitString1D s;
    if(!s.prepare(48000,0.328,48,0.0007,0.6))return false;
    const auto b=s.at(0.0328),f=s.at(0.23);
    double worst=0;
    for(int i=0;i<18000;++i){
        const double fb=0.08*std::sin(2*3.141592653589793*83*i/48000);
        const double ff=0.02*std::sin(2*3.141592653589793*37*i/48000);
        s.advance(s.begin(b,f),fb,ff);
        worst=std::max(worst,std::abs(s.ledgerResidual()));
    }
    std::cout<<"forced energy/work residual "<<worst<<" joule\n";
    return worst<1.0e-12;
}

bool movingFingerTest(double hz){
    BowedStringPilot s;if(!s.prepare(48000,hz))return false;
    Gesture g;g.bowSpeed=0.22;g.normalForce=0.28;
    g.fingerX=s.fingerPositionForFrequency(hz*std::pow(2.0,2.0/12.0));
    double sum=0,worst=0;int sticks=0,slips=0;
    for(int i=0;i<48000;++i){
        if(i>=16000&&i<32000)g.fingerLoad=std::min(1.0,(i-16000)/320.0);
        if(i>=32000)g.fingerLoad=std::max(0.0,1.0-(i-32000)/320.0);
        const double force=s.step(g);
        const double energy=s.mechanics().energy();
        worst=std::max(worst,std::abs(s.mechanics().ledgerResidual()));
        if(!std::isfinite(force)||!std::isfinite(energy)||
           energy>5||std::abs(force)>200)return false;
        sum+=std::abs(force);
        if(s.sticking())++sticks;else ++slips;
    }
    std::cout<<"bow "<<hz<<" mean reaction "<<sum/48000
             <<" stick "<<sticks<<" slip "<<slips
             <<" ledger "<<worst<<"\n";
    return sum/48000>0.01 && sticks>0 && slips>0 && worst<1.0e-10;
}

void writeDiagnostic(const char* path){
    BowedStringPilot s;if(!s.prepare(48000,440))return;
    Gesture g;g.bowSpeed=0.23;g.normalForce=0.30;
    g.fingerX=s.fingerPositionForFrequency(493.8833);
    std::vector<float> samples;samples.reserve(144000);
    for(int i=0;i<144000;++i){
        if(i>=48000&&i<96000)g.fingerLoad=std::min(1.0,(i-48000)/600.0);
        if(i>=96000)g.fingerLoad=std::max(0.0,1.0-(i-96000)/600.0);
        samples.push_back(float(std::clamp(s.step(g)*0.04,-0.95,0.95)));
    }
    std::ofstream o(path,std::ios::binary);if(!o)return;
    const std::uint32_t bytes=std::uint32_t(samples.size()*4);
    auto u16=[&](std::uint16_t n){o.write(reinterpret_cast<const char*>(&n),2);};
    auto u32=[&](std::uint32_t n){o.write(reinterpret_cast<const char*>(&n),4);};
    o.write("RIFF",4);u32(36+bytes);o.write("WAVEfmt ",8);
    u32(16);u16(3);u16(1);u32(48000);u32(48000*4);u16(4);u16(32);
    o.write("data",4);u32(bytes);
    o.write(reinterpret_cast<const char*>(samples.data()),bytes);
}

int main(int argc,char** argv){
    const bool ok=freeEnergyTest(44100,196)
        &&freeEnergyTest(48000,659.2551)
        &&workIdentityTest()
        &&movingFingerTest(196)
        &&movingFingerTest(440)
        &&movingFingerTest(659.2551);
    if(ok&&argc>1)writeDiagnostic(argv[1]);
    std::cout<<(ok?"Fiddle rebuild numeric PASS\n":"Fiddle rebuild numeric FAIL\n");
    return ok?EXIT_SUCCESS:EXIT_FAILURE;
}
