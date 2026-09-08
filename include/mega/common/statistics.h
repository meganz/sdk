#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace mega
{
namespace common
{
namespace detail
{

// Latches an input value if it satisfies a given predicate.
template<template<typename T> typename Predicate, typename T>
class Latcher
{
    // Current latch value.
    std::optional<T> mValue{};

public:
    // Feed a new value into our latch.
    T operator()(T value)
    {
        // Latch value as needed.
        if (!mValue || Predicate<T>()(value, *mValue))
            mValue = value;

        // Return latched value.
        return *mValue;
    }

    // Retrieve the latch's current value.
    std::optional<T> get() const
    {
        return mValue;
    }
}; // Latcher<Predicate<?>, T>

} // detail

// Computes a running average over a window of at most N values.
template<typename T, std::size_t N>
class Averager
{
    // The current running average.
    std::optional<double> mAverage{};

    // At what index does our window of values begin?
    std::size_t mBegin{};

    // How many values does our window contain?
    std::size_t mCount{};

    // The current running total of the last N input values.
    T mTotal{};

    // Keeps track of the last N input values.
    T mValues[N]{};

public:
    // Add a new value into our running average.
    double operator()(T value)
    {
        // Compute where we'll store the new value.
        auto index = (mBegin + mCount) % N;

        // Window already contains N values so rotate it.
        if (mCount == N)
        {
            // Remove oldest value from running total.
            mTotal -= mValues[mBegin];

            // Rotate window forward by one value.
            mBegin = (mBegin + 1) % N;
        }

        // Add value to the end of our window.
        mValues[index] = value;

        // Add value into our running total.
        mTotal += value;

        // Bump counter as necessary.
        mCount = std::min(mCount + 1, N);

        // Recompute running average.
        mAverage = static_cast<double>(mTotal) / static_cast<double>(mCount);

        // Return updated running average.
        return *mAverage;
    }

    // Retrieve the current running average.
    std::optional<double> get() const
    {
        return mAverage;
    }
}; // Averager<T, N>

/**
 * @brief Asymmetric Exponential Moving Average (EMA) for integer metrics.
 *
 * Designed for tracking highly erratic metrics (like network throughput or I/O latencies)
 * using efficient 8-bit fixed-point integer math, bypassing floating-point overhead.
 *
 * Asymmetry allows the filter to react at different speeds depending on whether the
 * signal is dropping or recovering. This prevents transient drops from permanently
 * tanking the metric while allowing controlled, steady recovery.
 *
 * Scale reference for Alphas (out of 256):
 *  - 26:  ~0.10 (Highly smoothed, filters out sharp spikes/drops)
 *  - 64:  = 0.25 (Balanced tracking)
 *  - 128: = 0.50 (Highly responsive, mirrors incoming jitter)
 */
class AsymmetricEma
{
public:
    AsymmetricEma(uint32_t alphaDown, uint32_t alphaUp):
        mAlphaDown(std::min(alphaDown, 256u)),
        mAlphaUp(std::min(alphaUp, 256u))
    {}

    void update(uint64_t newSample)
    {
        // Scale the raw incoming sample up by 256 (2^8) to match our internal fixed-point
        // resolution. This preserves the fractional component during the EMA multiplication step.
        uint64_t scaledSample = newSample << 8;

        // Cold-start initialization: baseline the average immediately to the first sample.
        if (!mCurrentEma)
        {
            mCurrentEma = scaledSample;
            return;
        }

        const auto current = mCurrentEma.value();

        // Standard EMA Formula: New = (Alpha * Sample) + ((1 - Alpha) * Previous)
        // Since both terms are scaled by 256 internally, their addition creates a temporary
        // "double-scaled" value (Value * 65536). We add 128 (half of 256) right before
        // shifting down to turn standard integer truncation into a true mathematical
        // round-to-nearest.
        if (scaledSample < current)
        {
            mCurrentEma = (mAlphaDown * scaledSample + (256 - mAlphaDown) * current + 128) >> 8;
        }
        else
        {
            mCurrentEma = (mAlphaUp * scaledSample + (256 - mAlphaUp) * current + 128) >> 8;
        }
    }

    /**
     * @brief Extracts the current smoothed value back into a standard integer.
     * @return The rounded, unscaled value tracked by the EMA.
     */
    uint64_t getValue() const
    {
        // Unscale the accumulator by dividing by 256 (>> 8).
        // Adding 128 enforces round-to-nearest, preventing a systematic downward integer bias.
        return mCurrentEma ? (*mCurrentEma + 128) >> 8 : 0;
    }

private:
    uint32_t mAlphaDown; // Scale of 256
    uint32_t mAlphaUp; // Scale of 256
    std::optional<uint64_t> mCurrentEma{}; // Stored in fixed-point (Value * 256)
};

// Incrementally computes the maximum in a sequence of values.
template<typename T>
using Maximizer = detail::Latcher<std::greater, T>;

// Incrementally computes the minimum in a sequence of values.
template<typename T>
using Minimizer = detail::Latcher<std::less, T>;

} // common
} // mega
