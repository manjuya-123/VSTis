#pragma once

#include "ModelConstants.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace fiddle::detail
{
struct BowGeometryResult
{
    std::array<double, stringCount> normalForceN{};
    double bowAngleDeg = 0.0;
};

class BowGeometryMapper
{
public:
    void prepare() noexcept
    {
        for (int pair = 0; pair < pairCount; ++pair)
        {
            for (int forceIndex = 0; forceIndex < forcePointCount; ++forceIndex)
            {
                const auto forceUnit = static_cast<double>(forceIndex)
                                     / static_cast<double>(forcePointCount - 1);
                const auto totalForce = maxMappedForceN * forceUnit * forceUnit;

                for (int balanceIndex = 0; balanceIndex < balancePointCount; ++balanceIndex)
                {
                    const auto balance = -1.0
                        + 2.0 * static_cast<double>(balanceIndex)
                        / static_cast<double>(balancePointCount - 1);

                    angleLut_[index(pair, forceIndex, balanceIndex)] =
                        totalForce <= 0.0
                            ? pairChordAngleDeg[static_cast<std::size_t>(pair)]
                            : calibrateAngle(pair, totalForce, balance);
                }
            }
        }
    }

    BowGeometryResult solve(int pairLower,
                            double totalForceN,
                            double balance) const noexcept
    {
        pairLower = std::clamp(pairLower, 0, pairCount - 1);
        totalForceN = std::clamp(totalForceN, 0.0, maxMappedForceN);
        balance = std::clamp(balance, -1.0, 1.0);

        const auto forceCoordinate = std::sqrt(
            totalForceN / maxMappedForceN) * static_cast<double>(forcePointCount - 1);
        const auto balanceCoordinate = 0.5 * (balance + 1.0)
                                     * static_cast<double>(balancePointCount - 1);

        const auto f0 = std::clamp(static_cast<int>(std::floor(forceCoordinate)),
                                   0, forcePointCount - 1);
        const auto f1 = std::min(f0 + 1, forcePointCount - 1);
        const auto b0 = std::clamp(static_cast<int>(std::floor(balanceCoordinate)),
                                   0, balancePointCount - 1);
        const auto b1 = std::min(b0 + 1, balancePointCount - 1);

        const auto ff = forceCoordinate - static_cast<double>(f0);
        const auto bf = balanceCoordinate - static_cast<double>(b0);

        const auto a00 = angleLut_[index(pairLower, f0, b0)];
        const auto a01 = angleLut_[index(pairLower, f0, b1)];
        const auto a10 = angleLut_[index(pairLower, f1, b0)];
        const auto a11 = angleLut_[index(pairLower, f1, b1)];

        const auto a0 = a00 + bf * (a01 - a00);
        const auto a1 = a10 + bf * (a11 - a10);

        BowGeometryResult result;
        result.bowAngleDeg = a0 + ff * (a1 - a0);
        result.normalForceN = forceVector(totalForceN, result.bowAngleDeg);
        return result;
    }

private:
    static constexpr int pairCount = 3;
    static constexpr int forcePointCount = 33;
    static constexpr int balancePointCount = 65;
    static constexpr double maxMappedForceN = 0.50;
    static constexpr double angleSearchHalfWidthDeg = 10.0;

    std::array<double,
        pairCount * forcePointCount * balancePointCount> angleLut_{};

    static constexpr std::size_t index(int pair,
                                       int forceIndex,
                                       int balanceIndex) noexcept
    {
        return static_cast<std::size_t>(
            (pair * forcePointCount + forceIndex) * balancePointCount + balanceIndex);
    }

    static std::array<double, stringCount>
    forceVector(double totalForceN, double angleDeg) noexcept
    {
        std::array<double, stringCount> result{};
        if (totalForceN <= 0.0)
            return result;

        const auto slope = std::tan(angleDeg * pi / 180.0);
        std::array<double, stringCount> gaps{};
        double highest = -1.0e30;

        for (std::size_t i = 0; i < stringCount; ++i)
        {
            gaps[i] = bridgeYmm[i] - slope * bridgeXmm[i];
            highest = std::max(highest, gaps[i]);
        }
        for (auto& gap : gaps)
            gap = highest - gap;

        const auto totalAtDepth = [&](double depth) noexcept
        {
            double sum = 0.0;
            for (const auto gap : gaps)
            {
                const auto indentation = std::max(0.0, depth - gap);
                sum += contactStiffness * std::pow(indentation, contactExponent);
            }
            return sum;
        };

        double lo = 0.0;
        double hi = 2.0;
        while (totalAtDepth(hi) < totalForceN && hi < 16.0)
            hi *= 2.0;

        for (int iteration = 0; iteration < 20; ++iteration)
        {
            const auto mid = 0.5 * (lo + hi);
            if (totalAtDepth(mid) < totalForceN)
                lo = mid;
            else
                hi = mid;
        }

        const auto depth = 0.5 * (lo + hi);
        for (std::size_t i = 0; i < stringCount; ++i)
        {
            const auto indentation = std::max(0.0, depth - gaps[i]);
            result[i] = contactStiffness * std::pow(indentation, contactExponent);
            if (result[i] < 1.0e-10)
                result[i] = 0.0;
        }

        return result;
    }

    static double desiredUpperFraction(int pairLower, double balance) noexcept
    {
        const auto lower = static_cast<std::size_t>(pairLower);
        const auto upper = lower + 1;
        const auto lowerWeight =
            stringImpedance[lower] * std::exp(-balanceSharpness * balance);
        const auto upperWeight =
            stringImpedance[upper] * std::exp(+balanceSharpness * balance);
        return upperWeight / (lowerWeight + upperWeight);
    }

    static double upperFraction(const std::array<double, stringCount>& force,
                                int pairLower) noexcept
    {
        const auto lower = static_cast<std::size_t>(pairLower);
        const auto upper = lower + 1;
        const auto pairForce = force[lower] + force[upper];
        return pairForce > 1.0e-15 ? force[upper] / pairForce : 0.5;
    }

    static double calibrateAngle(int pairLower,
                                 double totalForceN,
                                 double balance) noexcept
    {
        const auto baseAngle =
            pairChordAngleDeg[static_cast<std::size_t>(pairLower)];
        const auto target = desiredUpperFraction(pairLower, balance);

        double lo = baseAngle - angleSearchHalfWidthDeg;
        double hi = baseAngle + angleSearchHalfWidthDeg;

        // Positive angle favors the lower/left member of every adjacent pair,
        // so the upper-string force fraction decreases monotonically.
        for (int iteration = 0; iteration < 18; ++iteration)
        {
            const auto mid = 0.5 * (lo + hi);
            const auto fraction = upperFraction(
                forceVector(totalForceN, mid), pairLower);

            if (fraction > target)
                lo = mid;
            else
                hi = mid;
        }

        return 0.5 * (lo + hi);
    }
};
} // namespace fiddle::detail
