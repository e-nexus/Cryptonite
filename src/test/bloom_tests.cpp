// Copyright (c) 2012-2013 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Cryptonite-shape bloom filter tests. The original Bitcoin Core tests
// embedded real Bitcoin Core transactions and blocks in hex; those can't
// be deserialized under Cryptonite's CTransaction model (uint160 pubKey
// I/O instead of Bitcoin Core's COutPoint + script). This file exercises
// the same bloom-filter logic with Cryptonite-native transactions.

#include "bloom.h"

#include "base58.h"
#include "core.h"
#include "key.h"
#include "main.h"
#include "serialize.h"
#include "uint256.h"
#include "util.h"

#include <vector>

#include <boost/test/unit_test.hpp>

using namespace std;
using namespace boost::tuples;

BOOST_AUTO_TEST_SUITE(bloom_tests)

BOOST_AUTO_TEST_CASE(bloom_create_insert_serialize)
{
    CBloomFilter filter(3, 0.01, 0, BLOOM_UPDATE_ALL);

    filter.insert(ParseHex("99108ad8ed9bb6274d3980bab5a85c048f0950c8"));
    BOOST_CHECK_MESSAGE( filter.contains(ParseHex("99108ad8ed9bb6274d3980bab5a85c048f0950c8")), "BloomFilter doesn't contain just-inserted object!");
    // One bit different in first byte
    BOOST_CHECK_MESSAGE(!filter.contains(ParseHex("19108ad8ed9bb6274d3980bab5a85c048f0950c8")), "BloomFilter contains something it shouldn't!");

    filter.insert(ParseHex("b5a2c786d9ef4658287ced5914b37a1b4aa32eee"));
    BOOST_CHECK_MESSAGE(filter.contains(ParseHex("b5a2c786d9ef4658287ced5914b37a1b4aa32eee")), "BloomFilter doesn't contain just-inserted object (2)!");

    filter.insert(ParseHex("b9300670b4c5366e95b2699e8b18bc75e5f729c5"));
    BOOST_CHECK_MESSAGE(filter.contains(ParseHex("b9300670b4c5366e95b2699e8b18bc75e5f729c5")), "BloomFilter doesn't contain just-inserted object (3)!");

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    filter.Serialize(stream, SER_NETWORK, PROTOCOL_VERSION);

    vector<unsigned char> vch = ParseHex("03614e9b050000000000000001");
    vector<char> expected(vch.size());

    for (unsigned int i = 0; i < vch.size(); i++)
        expected[i] = (char)vch[i];

    BOOST_CHECK_EQUAL_COLLECTIONS(stream.begin(), stream.end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(bloom_create_insert_serialize_with_tweak)
{
    // Same test as bloom_create_insert_serialize, but we add a nTweak of 100
    CBloomFilter filter(3, 0.01, 2147483649, BLOOM_UPDATE_ALL);

    filter.insert(ParseHex("99108ad8ed9bb6274d3980bab5a85c048f0950c8"));
    BOOST_CHECK_MESSAGE( filter.contains(ParseHex("99108ad8ed9bb6274d3980bab5a85c048f0950c8")), "BloomFilter doesn't contain just-inserted object!");
    // One bit different in first byte
    BOOST_CHECK_MESSAGE(!filter.contains(ParseHex("19108ad8ed9bb6274d3980bab5a85c048f0950c8")), "BloomFilter contains something it shouldn't!");

    filter.insert(ParseHex("b5a2c786d9ef4658287ced5914b37a1b4aa32eee"));
    BOOST_CHECK_MESSAGE(filter.contains(ParseHex("b5a2c786d9ef4658287ced5914b37a1b4aa32eee")), "BloomFilter doesn't contain just-inserted object (2)!");

    filter.insert(ParseHex("b9300670b4c5366e95b2699e8b18bc75e5f729c5"));
    BOOST_CHECK_MESSAGE(filter.contains(ParseHex("b9300670b4c5366e95b2699e8b18bc75e5f729c5")), "BloomFilter doesn't contain just-inserted object (3)!");

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    filter.Serialize(stream, SER_NETWORK, PROTOCOL_VERSION);

    vector<unsigned char> vch = ParseHex("03ce4299050000000100008001");
    vector<char> expected(vch.size());

    for (unsigned int i = 0; i < vch.size(); i++)
        expected[i] = (char)vch[i];

    BOOST_CHECK_EQUAL_COLLECTIONS(stream.begin(), stream.end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(bloom_create_insert_key)
{
    string strSecret = string("5Kg1gnAjaLfKiwhhPpGS3QfRg2m6awQvaj98JCZBZQ5SuS2F15C");
    CBitcoinSecret vchSecret;
    BOOST_CHECK(vchSecret.SetString(strSecret));

    CKey key = vchSecret.GetKey();
    CPubKey pubkey = key.GetPubKey();
    vector<unsigned char> vchPubKey(pubkey.begin(), pubkey.end());

    CBloomFilter filter(2, 0.001, 0, BLOOM_UPDATE_ALL);
    filter.insert(vchPubKey);
    uint160 hash = pubkey.GetID();
    filter.insert(vector<unsigned char>(hash.begin(), hash.end()));

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    filter.Serialize(stream, SER_NETWORK, PROTOCOL_VERSION);

    vector<unsigned char> vch = ParseHex("038fc16b080000000000000001");
    vector<char> expected(vch.size());

    for (unsigned int i = 0; i < vch.size(); i++)
        expected[i] = (char)vch[i];

    BOOST_CHECK_EQUAL_COLLECTIONS(stream.begin(), stream.end(), expected.begin(), expected.end());
}

// Cryptonite-shape transactions exercising the bloom filter matching
// logic against CTransaction::IsRelevantAndUpdate. The transactions are
// built in-memory rather than deserialized from hardcoded hex, because
// Cryptonite's CTransaction has a different field layout from Bitcoin
// Core's (uint160 pubKey I/O instead of COutPoint + script).
//
// tx1 spends K1 -> K3; tx2 spends K1 -> K3; tx3 spends K3 -> K1.
namespace {
    struct BloomFixture {
        BloomFixture() {
            K1.SetHex("1111111111111111111111111111111111111111");
            K3.SetHex("3333333333333333333333333333333333333333");

            tx1.SetNull(); tx1.nVersion = 1;
            tx1.vin.push_back(CTxIn(K1, 100 * COIN));
            tx1.vout.push_back(CTxOut(99 * COIN, K3));

            tx2.SetNull(); tx2.nVersion = 1;
            tx2.vin.push_back(CTxIn(K1, 100 * COIN));
            tx2.vout.push_back(CTxOut(50 * COIN, K3));

            tx3.SetNull(); tx3.nVersion = 1;
            tx3.vin.push_back(CTxIn(K3, 99 * COIN));
            tx3.vout.push_back(CTxOut(98 * COIN, K1));
        }
        uint160 K1, K3;
        CTransaction tx1, tx2, tx3;
    };
}

BOOST_FIXTURE_TEST_CASE(bloom_match, BloomFixture)
{
    CBloomFilter filter(10, 0.000001, 0, BLOOM_UPDATE_ALL);

    // Match by tx hash.
    filter.insert(tx1.GetHash());
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(tx1, tx1.GetHash()),
                        "Simple Bloom filter didn't match tx hash");

    // Match by txout pubkey (BLOOM_UPDATE_ALL adds matched outpoints).
    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    vector<unsigned char> vchK3(K3.begin(), K3.end());
    filter.insert(vchK3);
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(tx1, tx1.GetHash()),
                        "Simple Bloom filter didn't match output pubkey");
    // tx2 also pays to K3 -> should match.
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(tx2, tx2.GetHash()),
                        "Bloom filter didn't add matched output");

    // Match by txin pubkey.
    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    vector<unsigned char> vchK1(K1.begin(), K1.end());
    filter.insert(vchK1);
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(tx1, tx1.GetHash()),
                        "Simple Bloom filter didn't match input pubkey");

    // No match: random pubkey not in tx1's I/O.
    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    uint160 KRandom;
    KRandom.SetHex("9999999999999999999999999999999999999999");
    vector<unsigned char> vchRand(KRandom.begin(), KRandom.end());
    filter.insert(vchRand);
    BOOST_CHECK_MESSAGE(!filter.IsRelevantAndUpdate(tx1, tx1.GetHash()),
                        "Simple Bloom filter matched random pubkey");

    // No match: random hash.
    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    filter.insert(uint256("0x9999999999999999999999999999999999999999999999999999999999999999"));
    BOOST_CHECK_MESSAGE(!filter.IsRelevantAndUpdate(tx1, tx1.GetHash()),
                        "Simple Bloom filter matched random tx hash");
}

BOOST_AUTO_TEST_SUITE_END()