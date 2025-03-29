
#include "SpiAnalyzer.h"
#include "SpiAnalyzerSettings.h"
#include <AnalyzerChannelData.h>
#include <vector>
#include <algorithm>

using namespace std;

#include <Windows.h>
#define MAX_LOG_BUF 512
void Log(char* fmt, ...)
{
	va_list ap;
	static char logbuf[MAX_LOG_BUF];
	SYSTEMTIME t;
	int l=0;

	memset(logbuf, 0, sizeof(logbuf));

	//GetLocalTime(&t);
	//l = sprintf(logbuf, "%02d/%02d %02d:%02d:%02d [%03d]  ",t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);

	va_start(ap, fmt);
	_vsnprintf(logbuf + l, MAX_LOG_BUF, fmt, ap);
	va_end(ap);

	OutputDebugString(logbuf);
}

SpiAnalyzer::SpiAnalyzer()
    :   Analyzer(),
        mSettings(new SpiAnalyzerSettings()),
        mSimulationInitilized(false),
        mMosi(NULL),
        mMiso(NULL),
        mClock(NULL),
        mEnable(NULL)
{
	mSampleCount = 0;
    SetAnalyzerSettings(mSettings.get());
}

SpiAnalyzer::~SpiAnalyzer()
{
    KillThread();
}

void SpiAnalyzer::SetupResults()
{
    mResults.reset(new SpiAnalyzerResults(this, mSettings.get()));
    SetAnalyzerResults(mResults.get());

    if (mSettings->mMosiChannel != UNDEFINED_CHANNEL) {
        mResults->AddChannelBubblesWillAppearOn(mSettings->mMosiChannel);
    }
    if (mSettings->mMisoChannel != UNDEFINED_CHANNEL) {
        mResults->AddChannelBubblesWillAppearOn(mSettings->mMisoChannel);
    }
}

//自动计算平均 clk 的时钟宽度，用于纠错初始值的计算, 
//返回值：0 无效，本次解码应退出
//       >0 平均时钟宽度

//BUG: AnalyzerChannelData *mClock 不能回退啊
//      所以损失前面一些波形
/*int SpiAnalyzer::AutoClock()
{
	int w = 0, pos1, pos2;
	U64 old = mClock->GetSampleNumber();
	U64 curr,prev,diff,total;
	std::vector<U64>  clks;
	int i;
	mClock->AdvanceToNextEdge();  //skip first edge
	prev = mClock->GetSampleNumber();
	for (i = 0; i < 16; i++)
	{
		mClock->AdvanceToNextEdge();
		curr = mClock->GetSampleNumber();
		diff = curr - prev;
		clks.push_back(diff);
		prev = curr;
	}

	std::sort(clks.begin(), clks.end());
	if (clks.size() < 16) goto exit;
	
	pos1 = (int)(clks.size() * 0.2);
	pos2 = (int)(clks.size() * 0.8);
	total = 0;
	for (i = pos1; i <= pos2; i++)
	{
		total += clks[i];
	}
	w = total / (pos2 - pos1 + 1);
	if (w < 10) w = 10;

exit:
	mClock->AdvanceToAbsPosition(old);
	return w;
}*/

void SpiAnalyzer::WorkerThread()
{
    Setup();

    mResults->CommitPacketAndStartNewPacket();
    mResults->CommitResults();

    if (mEnable != NULL) {
        if (mEnable->GetBitState() != mSettings->mEnableActiveState) {
            mEnable->AdvanceToNextEdge();
        }

        mCurrentSample = mEnable->GetSampleNumber();
        mClock->AdvanceToAbsPosition(mCurrentSample);
    } else {
        mCurrentSample = mClock->GetSampleNumber();
    }

    for (; ;) {
        if (IsInitialClockPolarityCorrect() == true) { //if false, this function moves to the next active enable edge.
            break;
        }
    }

	mNeedExit = false;	

    for (; ;) {
        GetWord();
		if (mNeedExit) break;
        CheckIfThreadShouldExit();
    }
}

void SpiAnalyzer::AdvanceToActiveEnableEdgeWithCorrectClockPolarity()
{
    mResults->CommitPacketAndStartNewPacket();
    mResults->CommitResults();

    AdvanceToActiveEnableEdge();

    for (; ;) {
        if (IsInitialClockPolarityCorrect() == true) { //if false, this function moves to the next active enable edge.
            break;
        }
    }
}

void SpiAnalyzer::Setup()
{
    bool allow_last_trailing_clock_edge_to_fall_outside_enable = false;
    if (mSettings->mDataValidEdge == AnalyzerEnums::LeadingEdge) {
        allow_last_trailing_clock_edge_to_fall_outside_enable = true;
    }

    if (mSettings->mClockInactiveState == BIT_LOW) {
        if (mSettings->mDataValidEdge == AnalyzerEnums::LeadingEdge) {
            mArrowMarker = AnalyzerResults::UpArrow;
        } else {
            mArrowMarker = AnalyzerResults::DownArrow;
        }

    } else {
        if (mSettings->mDataValidEdge == AnalyzerEnums::LeadingEdge) {
            mArrowMarker = AnalyzerResults::DownArrow;
        } else {
            mArrowMarker = AnalyzerResults::UpArrow;
        }
    }

    if (mSettings->mMosiChannel != UNDEFINED_CHANNEL) {
        mMosi = GetAnalyzerChannelData(mSettings->mMosiChannel);
    } else {
        mMosi = NULL;
    }

    if (mSettings->mMisoChannel != UNDEFINED_CHANNEL) {
        mMiso = GetAnalyzerChannelData(mSettings->mMisoChannel);
    } else {
        mMiso = NULL;
    }

    mClock = GetAnalyzerChannelData(mSettings->mClockChannel);

    if (mSettings->mEnableChannel != UNDEFINED_CHANNEL) {
        mEnable = GetAnalyzerChannelData(mSettings->mEnableChannel);
    } else {
        mEnable = NULL;
    }
}

void SpiAnalyzer::AdvanceToActiveEnableEdge()
{
    if (mEnable != NULL) {
        if (mEnable->GetBitState() != mSettings->mEnableActiveState) {
            mEnable->AdvanceToNextEdge();
        } else {
            mEnable->AdvanceToNextEdge();
            mEnable->AdvanceToNextEdge();
        }
        mCurrentSample = mEnable->GetSampleNumber();
        mClock->AdvanceToAbsPosition(mCurrentSample);
    } else {
        mCurrentSample = mClock->GetSampleNumber();
    }
}

bool SpiAnalyzer::IsInitialClockPolarityCorrect()
{
    if (mClock->GetBitState() == mSettings->mClockInactiveState) {
        return true;
    }

    if (mSettings->mShowMarker) {
        mResults->AddMarker(mCurrentSample, AnalyzerResults::ErrorSquare, mSettings->mClockChannel);
    }

    if (mEnable != NULL) {
        Frame error_frame;
        error_frame.mStartingSampleInclusive = mCurrentSample;

        mEnable->AdvanceToNextEdge();
        mCurrentSample = mEnable->GetSampleNumber();

        error_frame.mEndingSampleInclusive = mCurrentSample;
        error_frame.mFlags = SPI_ERROR_FLAG | DISPLAY_AS_ERROR_FLAG;
        mResults->AddFrame(error_frame);
        mResults->CommitResults();
        ReportProgress(error_frame.mEndingSampleInclusive);

        //move to the next active-going enable edge
        mEnable->AdvanceToNextEdge();
        mCurrentSample = mEnable->GetSampleNumber();
        mClock->AdvanceToAbsPosition(mCurrentSample);

        return false;
    } else {
        mClock->AdvanceToNextEdge();  //at least start with the clock in the idle state.
        mCurrentSample = mClock->GetSampleNumber();
        return true;
    }
}

bool SpiAnalyzer::WouldAdvancingTheClockToggleEnable()
{
    if (mEnable == NULL) {
        return false;
    }

    U64 next_edge = mClock->GetSampleOfNextEdge();
    bool enable_will_toggle = mEnable->WouldAdvancingToAbsPositionCauseTransition(next_edge);

    if (enable_will_toggle == false) {
        return false;
    } else {
        return true;
    }
}

double SpiAnalyzer::GetSec(U64 num)
{
	return (double)num / GetSampleRate();
}

int SpiAnalyzer::AutoClock(U64 clk[16])
{
	U64 total = 0, diff, prev;
	int i;
	vector<U64> clkv;
	prev = clk[0];
	for (i = 1; i < mSettings->mBitsPerTransfer; i++)
	{
		diff = clk[i] - clk[i - 1];
		clkv.push_back(diff);
	}
	std::sort(clkv.begin(), clkv.end());
	for (int i = 2; i <= 4; i++)
		total += clkv[i];
	total /= 3;
	if (total < 10) total = 10;
	mEc.MaxPulseWidth = total*4;
	mEc.MaxTwoPulseWidth = total * 8;
	return 0;
}

#define PUSH_ARROR_LOCATION(arr,location) do { al.Arrow=arr; al.Location=location; mArrowLocations.push_back(al); } while (0);

void SpiAnalyzer::GetWord()
{
	//we're assuming we come into this function with the clock in the idle state;

	U32 bits_per_transfer = mSettings->mBitsPerTransfer;

	DataBuilder mosi_result;
	U64 mosi_word = 0;
	mosi_result.Reset(&mosi_word, mSettings->mShiftOrder, bits_per_transfer);

	DataBuilder miso_result;
	U64 miso_word = 0;
	miso_result.Reset(&miso_word, mSettings->mShiftOrder, bits_per_transfer);

	U64 first_sample = 0;
	U64 last_sample = 0;
	U64 next_edge = 0;
	U64 bit_sample[16] = { 0 };
	bool need_reset = false;
	std::vector<ArrowLocations> mArrowLocations;
	ArrowLocations al;

	ReportProgress(mClock->GetSampleNumber());

	/*mClock->AdvanceToNextEdge();
	first_sample = mClock->GetSampleNumber();
	Log("%d,%d, %.8f\n",++mSampleCount, first_sample, GetSec(first_sample));
	if (mSampleCount > 2000) mNeedExit = true;
	return;*/


	for (U32 i = 0; i < bits_per_transfer; i++) {
		//on every single edge, we need to check that enable doesn't toggle.
		//note that we can't just advance the enable line to the next edge, becuase there may not be another edge

		if (WouldAdvancingTheClockToggleEnable() == true) {
			AdvanceToActiveEnableEdgeWithCorrectClockPolarity();  //ok, we pretty much need to reset everything and return.
			return;
		}

		mClock->AdvanceToNextEdge();
		mCurrentSample = mClock->GetSampleNumber();
		bit_sample[i] = mCurrentSample;

		if (i == 0) {
			first_sample = mCurrentSample;
			last_sample = first_sample;
			PUSH_ARROR_LOCATION(AnalyzerResults::Start, mCurrentSample);
		}
		else if (i == 7)	{
			PUSH_ARROR_LOCATION(AnalyzerResults::Stop, mCurrentSample);

			//自动计算clk
			if (mSampleCount == 0)
			{
				AutoClock(bit_sample);
			}
		}
		else {
			PUSH_ARROR_LOCATION(mArrowMarker, mCurrentSample);
		}
		

		next_edge = mClock->GetSampleOfNextEdge();

		//目前是写死的，实际上应该在setting里面设置一个clk值
		//然后根据clk算出才好
		//目前能用，先用之！！！
		if ( (next_edge - mCurrentSample>mEc.MaxPulseWidth) || (next_edge-last_sample>mEc.MaxTwoPulseWidth) )
		{
			return;
		}
		last_sample = mCurrentSample;

		if (mSettings->mDataValidEdge == AnalyzerEnums::LeadingEdge) {
			if (mMosi != NULL) {
				mMosi->AdvanceToAbsPosition(mCurrentSample);
				mosi_result.AddBit(mMosi->GetBitState());
			}
			if (mMiso != NULL) {
				mMiso->AdvanceToAbsPosition(mCurrentSample);
				miso_result.AddBit(mMiso->GetBitState());
			}
		}

		// ok, the trailing edge is messy -- but only on the very last bit.
		// If the trialing edge isn't doesn't represent valid data, we want to allow the enable line to rise before the clock trialing edge -- and still report the frame
		if ((i == (bits_per_transfer - 1)) && (mSettings->mDataValidEdge != AnalyzerEnums::TrailingEdge)) {
			//if this is the last bit, and the trailing edge doesn't represent valid data
			if (WouldAdvancingTheClockToggleEnable() == true) {
				//moving to the trailing edge would cause the clock to revert to inactive.  jump out, record the frame, and them move to the next active enable edge
				need_reset = true;
				break;
			}

			//enable isn't going to go inactive, go ahead and advance the clock as usual.  Then we're done, jump out and record the frame.
			mClock->AdvanceToNextEdge();
			break;
		}

		//this isn't the very last bit, etc, so proceed as normal
		if (WouldAdvancingTheClockToggleEnable() == true) {
			AdvanceToActiveEnableEdgeWithCorrectClockPolarity();  //ok, we pretty much need to reset everything and return.
			return;
		}

		//定位到下一个边沿
		mClock->AdvanceToNextEdge();
		mCurrentSample = mClock->GetSampleNumber();

		if (mSettings->mDataValidEdge == AnalyzerEnums::TrailingEdge) {
			//TODO: 需要判断这个采样时间超长，如果超长，则结束本字节            
			if (mMosi != NULL) {
				mMosi->AdvanceToAbsPosition(mCurrentSample);
				mosi_result.AddBit(mMosi->GetBitState());
			}
			if (mMiso != NULL) {
				mMiso->AdvanceToAbsPosition(mCurrentSample);
				miso_result.AddBit(mMiso->GetBitState());
			}
		}

	}

	//save the resuls:
	U32 count = mArrowLocations.size();
	if (mSettings->mShowMarker) {
		for (U32 i = 0; i < count; i++)
			mResults->AddMarker(mArrowLocations[i].Location, mArrowLocations[i].Arrow, mSettings->mClockChannel);
	}

	Frame result_frame;
	result_frame.mStartingSampleInclusive = first_sample;
	result_frame.mEndingSampleInclusive = mClock->GetSampleNumber();
	result_frame.mData1 = mosi_word;
	result_frame.mData2 = miso_word;
	result_frame.mFlags = 0;
	mResults->AddFrame(result_frame);

	mResults->CommitResults();
	mSampleCount++;

	if (need_reset == true) {
		AdvanceToActiveEnableEdgeWithCorrectClockPolarity();
	}
}

bool SpiAnalyzer::NeedsRerun()
{
    return false;
}

U32 SpiAnalyzer::GenerateSimulationData(U64 minimum_sample_index, U32 device_sample_rate, SimulationChannelDescriptor **simulation_channels)
{
    if (mSimulationInitilized == false) {
        mSimulationDataGenerator.Initialize(GetSimulationSampleRate(), mSettings.get());
        mSimulationInitilized = true;
    }

    return mSimulationDataGenerator.GenerateSimulationData(minimum_sample_index, device_sample_rate, simulation_channels);
}


U32 SpiAnalyzer::GetMinimumSampleRateHz()
{
    return 10000; //we don't have any idea, depends on the SPI rate, etc.; return the lowest rate.
}

const char *SpiAnalyzer::GetAnalyzerName() const
{
    return "SPI-EC";
}

const char *GetAnalyzerName()
{
    return "SPI-EC";
}

Analyzer *CreateAnalyzer()
{
    return new SpiAnalyzer();
}

void DestroyAnalyzer(Analyzer *analyzer)
{
    delete analyzer;
}
