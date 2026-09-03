// Copyright (c) 2012-2013 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "util.h"

#include <stdint.h>

#include <boost/test/unit_test.hpp>

// amounts 0.00000001 .. 0.00100000
#define NUM_MULTIPLES_UNIT 100000

// amounts 0.01 .. 100.00
#define NUM_MULTIPLES_CENT 10000

// amounts 1 .. 10000
#define NUM_MULTIPLES_1BTC 10000

// amounts 50 .. 21000000
#define NUM_MULTIPLES_50BTC 420000

BOOST_AUTO_TEST_SUITE(compress_tests)

bool static TestEncode(uint64_t in) {
    return in == CTxOutCompressor::DecompressAmount(CTxOutCompressor::CompressAmount(in));
}

bool static TestDecode(uint64_t in) {
    return in == CTxOutCompressor::CompressAmount(CTxOutCompressor::DecompressAmount(in));
}

bool static TestPair(uint64_t dec, uint64_t enc) {
    return CTxOutCompressor::CompressAmount(dec) == enc &&
           CTxOutCompressor::DecompressAmount(enc) == dec;
}

BOOST_AUTO_TEST_CASE(compress_amounts)
{
    // Cryptonite uses COIN=1e10 and CENT=1e8 (see util.h), versus Bitcoin
    // Core's COIN=1e8/CENT=1e6. The CTxOutCompressor algorithm itself is
    // identical (see core.cpp CompressAmount). These expected values are
    // computed step-by-step from the algorithm:
    //
    //   CENT=1e8:   strip trailing zeros -> e=8, n=1, d=1, n/=10 -> 0
    //               return 1 + (0*9 + 1 - 1)*10 + 8 = 9
    //   COIN=1e10:  strip trailing zeros -> e=9 (clamped), n=10
    //               return 1 + (10-1)*10 + 9 = 100
    //   50*COIN:    strip trailing zeros -> e=9 (clamped), n=500
    //               return 1 + (500-1)*10 + 9 = 5000
    //   21000000*COIN: strip trailing zeros -> e=9 (clamped), n=21000000
    //                 return 1 + (21000000-1)*10 + 9 = 2100000000
    BOOST_CHECK(TestPair(              0,          0));
    BOOST_CHECK(TestPair(              1,          1));
    BOOST_CHECK(TestPair(           CENT,          9));
    BOOST_CHECK(TestPair(           COIN,        100));
    BOOST_CHECK(TestPair(        50*COIN,       5000));
    BOOST_CHECK(TestPair(  21000000*COIN, 2100000000));

    for (uint64_t i = 1; i <= NUM_MULTIPLES_UNIT; i++)
        BOOST_CHECK(TestEncode(i));

    for (uint64_t i = 1; i <= NUM_MULTIPLES_CENT; i++)
        BOOST_CHECK(TestEncode(i * CENT));

    for (uint64_t i = 1; i <= NUM_MULTIPLES_1BTC; i++)
        BOOST_CHECK(TestEncode(i * COIN));

    for (uint64_t i = 1; i <= NUM_MULTIPLES_50BTC; i++)
        BOOST_CHECK(TestEncode(i * 50 * COIN));

    for (uint64_t i = 0; i < 100000; i++)
        BOOST_CHECK(TestDecode(i));
}

BOOST_AUTO_TEST_SUITE_END()
