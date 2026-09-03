// Copyright (c) 2014 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "core.h"
#include "main.h"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(main_tests)

// Cryptonite uses continuous inflation (see main.cpp GetBlockValue):
//     subsidy(coinbase) = coinbase * (243*COIN + 10*CENT) / MAX_MONEY
// The coefficient (243*COIN + 10*CENT) sets the per-block subsidy at the
// saturation point coinbase == MAX_MONEY/COEFF, beyond which MPZ arithmetic
// returns MAX_MONEY-clamped values. MAX_MONEY is the integration cap and
// equals UINT64_MAX rounded down to a whole number of COINS (core.h, util.h).
BOOST_AUTO_TEST_CASE(subsidy_limit_test)
{
    const uint64_t COEFF = 243 * COIN + 10 * CENT;
    BOOST_CHECK(COEFF > 0);
    BOOST_CHECK(COEFF < MAX_MONEY);

    uint64_t nSum = 0;
    for (int nHeight = 0; nHeight < 14000000; nHeight += 1000) {
        uint64_t nSubsidy = GetBlockValue(nHeight, 0);

        // Per-block subsidy must stay in [0, MAX_MONEY] regardless of input.
        BOOST_CHECK(nSubsidy <= MAX_MONEY);

        nSum += nSubsidy * 1000;
        BOOST_CHECK(MoneyRange(nSum));
    }

    // Sum must remain within MAX_MONEY regardless of how far we iterate
    // (every per-block subsidy fits inside MAX_MONEY, so the sum fits too).
    BOOST_CHECK(nSum <= MAX_MONEY);
}

BOOST_AUTO_TEST_SUITE_END()