#ifndef SPI_ANALYZER_H
#define SPI_ANALYZER_H

#include <Analyzer.h>
#include "SpiAnalyzerResults.h"
#include "SpiSimulationDataGenerator.h"

class SpiAnalyzerSettings;

//单次解码时标记箭头
typedef struct  {
	U64 Location;
	AnalyzerResults::MarkerType Arrow;
} ArrowLocations;

//纠错变量
typedef struct {
	int MaxPulseWidth;
	int MaxTwoPulseWidth;
} ErrorConditions;

class ANALYZER_EXPORT SpiAnalyzer : public Analyzer
{
public:
    SpiAnalyzer();
    virtual ~SpiAnalyzer();
    virtual void SetupResults();
    virtual void WorkerThread();

    virtual U32 GenerateSimulationData(U64 newest_sample_requested, U32 sample_rate, SimulationChannelDescriptor **simulation_channels);
    virtual U32 GetMinimumSampleRateHz();

    virtual const char *GetAnalyzerName() const;
    virtual bool NeedsRerun();

protected: //functions
    void Setup();
    void AdvanceToActiveEnableEdge();
    bool IsInitialClockPolarityCorrect();
    void AdvanceToActiveEnableEdgeWithCorrectClockPolarity();
    bool WouldAdvancingTheClockToggleEnable();
    void GetWord();
	double GetSec(U64 num);

#pragma warning( push )
#pragma warning( disable : 4251 ) //warning C4251: 'SerialAnalyzer::<...>' : class <...> needs to have dll-interface to be used by clients of class
protected:  //vars
    std::auto_ptr< SpiAnalyzerSettings > mSettings;
    std::auto_ptr< SpiAnalyzerResults > mResults;
    bool mSimulationInitilized;
    SpiSimulationDataGenerator mSimulationDataGenerator;

    AnalyzerChannelData *mMosi;
    AnalyzerChannelData *mMiso;
    AnalyzerChannelData *mClock;
    AnalyzerChannelData *mEnable;

    U64 mCurrentSample;
	U64 mSampleCount;
	bool mNeedExit;

    AnalyzerResults::MarkerType mArrowMarker;
	ErrorConditions mEc;
	int AutoClock(U64 u[16]);

#pragma warning( pop )
};

extern "C" ANALYZER_EXPORT const char *__cdecl GetAnalyzerName();
extern "C" ANALYZER_EXPORT Analyzer *__cdecl CreateAnalyzer();
extern "C" ANALYZER_EXPORT void __cdecl DestroyAnalyzer(Analyzer *analyzer);

#endif //SPI_ANALYZER_H
