#include "Dsp/BowStrokePolicy.h"

#include <cstdlib>
#include <iostream>

int main()
{
    using fiddle::BowStrokeMode;
    using fiddle::shouldAlternateBow;

    if (!shouldAlternateBow(BowStrokeMode::FiddleAuto, false))
    {
        std::cerr << "FAIL: separate note should alternate in Fiddle Auto\n";
        return EXIT_FAILURE;
    }

    if (shouldAlternateBow(BowStrokeMode::FiddleAuto, true))
    {
        std::cerr << "FAIL: overlapping legato note should stay connected in Fiddle Auto\n";
        return EXIT_FAILURE;
    }

    if (shouldAlternateBow(BowStrokeMode::Connected, false)
        || shouldAlternateBow(BowStrokeMode::Connected, true))
    {
        std::cerr << "FAIL: Connected should never auto-reverse\n";
        return EXIT_FAILURE;
    }

    if (!shouldAlternateBow(BowStrokeMode::Alternate, false)
        || !shouldAlternateBow(BowStrokeMode::Alternate, true))
    {
        std::cerr << "FAIL: Alternate should reverse on every Note On\n";
        return EXIT_FAILURE;
    }

    std::cout << "PASS\n";
    return EXIT_SUCCESS;
}
