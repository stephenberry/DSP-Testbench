/*
  ==============================================================================

    FftProcessor.h
    Created: 29 Jan 2018 10:20:18pm
    Author:  Andrew Jerrim

  ==============================================================================
*/

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "AudioDataTransfer.h"

/**
	This class inherits from FixedBlockProcessor so that it can run on the audio processing thread and allow a FFT to
	be computed on a fixed block size, regardless of the block sized used by the audio device or host. An AudioProbe object
	is then used to make the processed data available for use on other threads.
*/
template <int Order>
class FftProcessor final : public FixedBlockProcessor
{
public:

	// This FftFrame is necessary for us to use the AudioProbe class and is why this class (& therefore FftScope) is templated.
    struct FftFrame final
    {
		alignas(16) float f[1 << Order];
	};

    explicit FftProcessor();
    ~FftProcessor () = default;

    void prepare (const dsp::ProcessSpec& spec) override;
    void performProcessing (const int channel) override;
    
    void addListener (typename AudioProbe<FftFrame>::Listener* listener);
    void removeListener (typename AudioProbe<FftFrame>::Listener* listener);

    /** Copy frame of FFT frequency data */
    void copyFrequencyFrame (float* dest, const int channel) const;

    /** Copy frame of FFT phase data */
	void copyPhaseFrame (float* dest, const int channel) const;

    /** Call this to choose a different windowing method (class is initialised with Hann) */
    void setWindowingMethod (dsp::WindowingFunction<float>::WindowingMethod);

    /** Returns true if the referenced probe is owned by this object */
    bool ownsProbe (AudioProbe<FftFrame>* audioProbe) const;

private:

    dsp::FFT fft;
    const int size;
	AudioSampleBuffer temp;
	AudioSampleBuffer window;
    float amplitudeCorrectionFactor = 1.0f;

    OwnedArray <AudioProbe <FftFrame>> freqProbes;;
    OwnedArray <AudioProbe <FftFrame>> phaseProbes;

    ListenerList<typename AudioProbe<FftFrame>::Listener> listeners;
};


// ===========================================================================================
// Template implementations
// ===========================================================================================

template <int Order>
FftProcessor<Order>::FftProcessor (): FixedBlockProcessor (1 << Order),
                                      fft (Order),
                                      size (1 << Order)
{
    temp.setSize (1, size * 2, false, true);
    window.setSize (1, size);

    setWindowingMethod(dsp::WindowingFunction<float>::hann);
}

template <int Order>
void FftProcessor<Order>::prepare (const dsp::ProcessSpec& spec)
{
    FixedBlockProcessor::prepare (spec);

    freqProbes.clear();
    phaseProbes.clear();

    // Add probes for each channel to transfer audio data to the GUI
    for (auto ch = 0; ch < static_cast<int> (spec.numChannels); ++ch)
    {
        freqProbes.add(new AudioProbe<FftFrame>());
        phaseProbes.add (new AudioProbe<FftFrame>());
    }

    // But add listeners to the last channel only (prevents excessive paint calls)
    const auto lastChannel = static_cast<int>(spec.numChannels) - 1;
    for (auto i = 0; i < listeners.size(); ++i)
    {
        freqProbes[lastChannel]->addListener(listeners.getListeners()[i]);
        phaseProbes[lastChannel]->addListener (listeners.getListeners()[i]);
    }
}

template <int Order>
void FftProcessor<Order>::performProcessing (const int channel)
{
    temp.copyFrom(0, 0, buffer.getReadPointer(channel), size);
    FloatVectorOperations::multiply(temp.getWritePointer(0), window.getWritePointer(0), size);
    fft.performFrequencyOnlyForwardTransform(temp.getWritePointer(0));
    FloatVectorOperations::multiply (temp.getWritePointer(0), amplitudeCorrectionFactor, size);
    freqProbes[channel]->writeFrame(reinterpret_cast<const FftFrame*>(temp.getReadPointer(0)));
    phaseProbes[channel]->writeFrame(reinterpret_cast<const FftFrame*> (temp.getReadPointer (0) + size));
}

template <int Order>
void FftProcessor<Order>::copyFrequencyFrame (float* dest, const int channel) const
{
    freqProbes[channel]->copyFrame(reinterpret_cast<FftFrame*>(dest));
}

template <int Order>
void FftProcessor<Order>::copyPhaseFrame (float* dest, const int channel) const
{
	phaseProbes[channel]->copyFrame (reinterpret_cast <FftFrame*> (dest));
}

template <int Order>
void FftProcessor<Order>::setWindowingMethod (dsp::WindowingFunction<float>::WindowingMethod)
{
    const auto windowType = dsp::WindowingFunction<float>::hann;
    dsp::WindowingFunction<float>::fillWindowingTables (window.getWritePointer (0), static_cast<size_t> (size), windowType);

    auto windowIntegral = 0.0f;
    for (auto i = 0; i < size; ++i)
        windowIntegral += window.getWritePointer(0)[i];

    amplitudeCorrectionFactor = 2.0f / windowIntegral;
}

template <int Order>
void FftProcessor<Order>::addListener (typename AudioProbe<FftFrame>::Listener* listener)
{
    listeners.add(listener);
}

template <int Order>
void FftProcessor<Order>::removeListener (typename AudioProbe<FftFrame>::Listener* listener)
{
    listeners.remove(listener);
}

template <int Order>
bool FftProcessor<Order>::ownsProbe (AudioProbe<FftFrame>* audioProbe) const
{
    auto owned = false;
    for (auto p : freqProbes)
        if (audioProbe == p)
        {
            owned = true;
            break;
        }
    if (!owned)
    {
        for (auto p : phaseProbes)
            if (audioProbe == p)
            {
                owned = true;
                break;
            }
    }
    return owned;
}