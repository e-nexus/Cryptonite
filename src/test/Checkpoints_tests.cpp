// Copyright (c) 2011-2013 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for block-chain checkpoints
//

#include "checkpoints.h"

#include "uint256.h"

#include <boost/test/unit_test.hpp>

using namespace std;

// Cryptonite ships only the genesis block (height 0) in its compiled-in
// checkpoint map (see checkpoints.cpp). The semantic guarantee of the
// Checkpoints subsystem is:
//   - CheckBlock(h, hash) returns true when h is not in the checkpoint map
//   - CheckBlock(h, hash) returns (hash == expected) when h is in the map
//   - GetTotalBlocksEstimate() returns the height of the last checkpoint
//     (0 here, since only genesis is present)
//
// These invariants must hold regardless of which compiled-in checkpoints
// exist, so the test exercises them against arbitrary heights/hashes
// rather than asserting specific Bitcoin Core mainnet checkpoints.
BOOST_AUTO_TEST_SUITE(Checkpoints_tests)

BOOST_AUTO_TEST_CASE(sanity)
{
    const uint256 hashGenesis    = uint256("0x000009a460ccc429ac6e53c91c6ed2d96697884b8b656a903042faff8971c5aa");
    const uint256 hashGenesisAlt = uint256("0x00000000000005b12ffd4cd315cd34ffd4a594f430ac814c91184a0d42d2b0fe");
    const uint256 hashRandom     = uint256("0x1111111111111111111111111111111111111111111111111111111111111111");

    // Genesis is in the checkpoint map; matching hash must verify, non-matching must not.
    BOOST_CHECK( Checkpoints::CheckBlock(0, hashGenesis));
    BOOST_CHECK(!Checkpoints::CheckBlock(0, hashGenesisAlt));

    // Heights outside the checkpoint map must always pass, regardless of hash.
    BOOST_CHECK( Checkpoints::CheckBlock(1, hashGenesis));
    BOOST_CHECK( Checkpoints::CheckBlock(11111, hashGenesis));
    BOOST_CHECK( Checkpoints::CheckBlock(11111, hashGenesisAlt));
    BOOST_CHECK( Checkpoints::CheckBlock(134444, hashGenesisAlt));
    BOOST_CHECK( Checkpoints::CheckBlock(999999, hashRandom));

    // Total blocks estimate is bounded by the last checkpoint height.
    BOOST_CHECK(Checkpoints::GetTotalBlocksEstimate() >= 0);
    BOOST_CHECK(Checkpoints::GetTotalBlocksEstimate() <  134444);
}

BOOST_AUTO_TEST_SUITE_END()