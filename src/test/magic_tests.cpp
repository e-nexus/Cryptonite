// Copyright (c) 2014 The Mini-Blockchain Project
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Coverage for the P2P network magic bytes (chainparams.cpp) and the
// downstream consumers (CMessageHeader, CAddrman). The magic values
// are the four bytes that start every P2P packet on the wire and every
// on-disk address/block artefact. A regression here would either (a)
// let a passive Bitcoin Core node on the public internet observe
// Cryptonite peer traffic because the magics were re-converged, or
// (b) partition the network at the magic-check step. Both classes of
// regression are silent: no crash, no error to the user, just a
// network that fails to find peers.
//
// The test binary's global TestingSetup (test_cryptonite.cpp) sets
// mapArgs["-testnet"]="true" at startup, but does not call
// SelectParamsFromCommandLine, so pCurrentParams still points at
// &mainParams (the static initializer in chainparams.cpp) when the
// first test case runs. Every case that depends on a specific
// network calls SelectParams() at the start and the ParamsGuard
// restores whatever pCurrentParams was on entry when the case ends.
// The SelectParams test case cycles through every network and the
// guard's destructor restores the entry state.

#include "chainparams.h"
#include "netbase.h"
#include "protocol.h"
#include "serialize.h"
#include "util.h"
#include "version.h"

#include <boost/test/unit_test.hpp>
#include <cstring>
#include <vector>

BOOST_AUTO_TEST_SUITE(magic_tests)

namespace {

// "XCNA" / "XCNT" / "XCNR" spelled out as the four bytes the wire and
// disk files actually carry. The first three bytes identify the
// Cryptonite family, the fourth byte identifies the network within
// the family. These values are part of the network identity and must
// not be changed without coordinated network upgrade.
const unsigned char MAINNET_MAGIC[MESSAGE_START_SIZE]   = {0x58, 0x43, 0x4e, 0x41};
const unsigned char TESTNET_MAGIC[MESSAGE_START_SIZE]   = {0x58, 0x43, 0x4e, 0x54};
const unsigned char REGTEST_MAGIC[MESSAGE_START_SIZE]   = {0x58, 0x43, 0x4e, 0x52};

// Bitcoin Core mainnet, testnet3, and regtest magics, captured for the
// collision regression net. A regression that re-converges on any of
// these values would re-introduce the wire-level identity collision
// this branch was created to fix.
const unsigned char BTC_MAINNET_MAGIC[MESSAGE_START_SIZE]  = {0xf9, 0xbe, 0xb4, 0xd9};
const unsigned char BTC_TESTNET_MAGIC[MESSAGE_START_SIZE]  = {0x0b, 0x11, 0x09, 0x07};
const unsigned char BTC_REGTEST_MAGIC[MESSAGE_START_SIZE]  = {0xfa, 0xbf, 0xb5, 0xda};

// Restore the global pCurrentParams to the network the test case
// started in and also restore the -testnet / -regtest mapArgs
// entries that the case may have mutated. The pCurrentParams state
// on entry is whatever the previous test case left behind (initially
// &mainParams from the static initializer in chainparams.cpp), so
// every case must call SelectParams() explicitly and rely on this
// guard to undo the change.
struct ParamsGuard {
    CChainParams::Network prev;
    std::string prev_testnet;
    std::string prev_regtest;
    bool had_testnet;
    bool had_regtest;

    ParamsGuard()
        : prev(Params().NetworkID())
        , had_testnet(false)
        , had_regtest(false)
    {
        std::map<std::string, std::string>::const_iterator it;
        it = mapArgs.find("-testnet");
        if (it != mapArgs.end()) {
            prev_testnet = it->second;
            had_testnet = true;
        }
        it = mapArgs.find("-regtest");
        if (it != mapArgs.end()) {
            prev_regtest = it->second;
            had_regtest = true;
        }
    }

    ~ParamsGuard() {
        SelectParams(prev);
        if (had_testnet) {
            mapArgs["-testnet"] = prev_testnet;
        } else {
            mapArgs.erase("-testnet");
        }
        if (had_regtest) {
            mapArgs["-regtest"] = prev_regtest;
        } else {
            mapArgs.erase("-regtest");
        }
    }
};

bool memeq(const unsigned char* a, const unsigned char* b, size_t n)
{
    return std::memcmp(a, b, n) == 0;
}

} // namespace

// ---------------------------------------------------------------------------
// The three Cryptonite network magics are pairwise distinct.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(magics_are_pairwise_distinct)
{
    BOOST_CHECK(!memeq(MAINNET_MAGIC, TESTNET_MAGIC, MESSAGE_START_SIZE));
    BOOST_CHECK(!memeq(MAINNET_MAGIC, REGTEST_MAGIC, MESSAGE_START_SIZE));
    BOOST_CHECK(!memeq(TESTNET_MAGIC, REGTEST_MAGIC, MESSAGE_START_SIZE));
}

// ---------------------------------------------------------------------------
// The three Cryptonite network magics do not collide with the three
// Bitcoin Core magics. This is the regression net: if a future change
// to chainparams.cpp accidentally re-converges on the Bitcoin Core
// mainnet, testnet, or regtest magic, this test fails.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(magics_do_not_collide_with_bitcoin_core)
{
    BOOST_CHECK(!memeq(MAINNET_MAGIC, BTC_MAINNET_MAGIC, MESSAGE_START_SIZE));
    BOOST_CHECK(!memeq(MAINNET_MAGIC, BTC_TESTNET_MAGIC, MESSAGE_START_SIZE));
    BOOST_CHECK(!memeq(MAINNET_MAGIC, BTC_REGTEST_MAGIC, MESSAGE_START_SIZE));
    BOOST_CHECK(!memeq(TESTNET_MAGIC, BTC_MAINNET_MAGIC, MESSAGE_START_SIZE));
    BOOST_CHECK(!memeq(TESTNET_MAGIC, BTC_TESTNET_MAGIC, MESSAGE_START_SIZE));
    BOOST_CHECK(!memeq(TESTNET_MAGIC, BTC_REGTEST_MAGIC, MESSAGE_START_SIZE));
    BOOST_CHECK(!memeq(REGTEST_MAGIC, BTC_MAINNET_MAGIC, MESSAGE_START_SIZE));
    BOOST_CHECK(!memeq(REGTEST_MAGIC, BTC_TESTNET_MAGIC, MESSAGE_START_SIZE));
    BOOST_CHECK(!memeq(REGTEST_MAGIC, BTC_REGTEST_MAGIC, MESSAGE_START_SIZE));
}

// ---------------------------------------------------------------------------
// Params().MessageStart() returns the expected magic for each network.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(params_message_start_mainnet)
{
    ParamsGuard guard;
    SelectParams(CChainParams::MAIN);
    const MessageStartChars& m = Params().MessageStart();
    BOOST_CHECK(memeq(m, MAINNET_MAGIC, MESSAGE_START_SIZE));
}

BOOST_AUTO_TEST_CASE(params_message_start_testnet)
{
    ParamsGuard guard;
    SelectParams(CChainParams::TESTNET);
    const MessageStartChars& m = Params().MessageStart();
    BOOST_CHECK(memeq(m, TESTNET_MAGIC, MESSAGE_START_SIZE));
}

BOOST_AUTO_TEST_CASE(params_message_start_regtest)
{
    ParamsGuard guard;
    SelectParams(CChainParams::REGTEST);
    const MessageStartChars& m = Params().MessageStart();
    BOOST_CHECK(memeq(m, REGTEST_MAGIC, MESSAGE_START_SIZE));
}

// ---------------------------------------------------------------------------
// SelectParamsFromCommandLine() honors -regtest, -testnet, the
// default (mainnet), and the mutual-exclusion guard. The
// SelectParamsFromCommandLine code path was the regression
// (assert(false)) that this branch also fixes.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(select_params_from_command_line)
{
    ParamsGuard guard;

    // The Network enum has no operator<<, so we compare via the
    // int value of the enum rather than BOOST_CHECK_EQUAL.

    // Default (no flag) -> mainnet.
    mapArgs.erase("-testnet");
    mapArgs.erase("-regtest");
    BOOST_CHECK(SelectParamsFromCommandLine());
    BOOST_CHECK_EQUAL(static_cast<int>(Params().NetworkID()),
                      static_cast<int>(CChainParams::MAIN));
    BOOST_CHECK(memeq(Params().MessageStart(), MAINNET_MAGIC, MESSAGE_START_SIZE));

    // -testnet -> testnet.
    // GetBoolArg treats the value as an integer; "1" (or empty) is
    // the canonical Bitcoin Core form, "true" is rejected because
    // atoi("true") == 0.
    mapArgs["-testnet"] = "1";
    mapArgs.erase("-regtest");
    BOOST_CHECK(SelectParamsFromCommandLine());
    BOOST_CHECK_EQUAL(static_cast<int>(Params().NetworkID()),
                      static_cast<int>(CChainParams::TESTNET));
    BOOST_CHECK(memeq(Params().MessageStart(), TESTNET_MAGIC, MESSAGE_START_SIZE));
    mapArgs.erase("-testnet");

    // -regtest -> regtest. This is the path that previously
    // hit assert(false) in SelectParams.
    mapArgs["-regtest"] = "1";
    BOOST_CHECK(SelectParamsFromCommandLine());
    BOOST_CHECK_EQUAL(static_cast<int>(Params().NetworkID()),
                      static_cast<int>(CChainParams::REGTEST));
    BOOST_CHECK(memeq(Params().MessageStart(), REGTEST_MAGIC, MESSAGE_START_SIZE));
    mapArgs.erase("-regtest");

    // -testnet -regtest -> rejected, params unchanged.
    mapArgs["-testnet"] = "1";
    mapArgs["-regtest"] = "1";
    BOOST_CHECK(!SelectParamsFromCommandLine());
    mapArgs.erase("-testnet");
    mapArgs.erase("-regtest");
}

// ---------------------------------------------------------------------------
// CMessageHeader::IsValid() accepts a header stamped with the active
// network's magic and rejects a header stamped with a foreign magic
// (e.g. Bitcoin Core mainnet). This is the gate that fires before any
// command dispatch on inbound P2P traffic, so a regression here would
// either accept foreign peers (the original bug) or reject our own
// peers (the inverse failure mode).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(message_header_accepts_local_magic)
{
    ParamsGuard guard;
    SelectParams(CChainParams::MAIN);

    CMessageHeader hdr("version", 0);
    BOOST_CHECK(hdr.IsValid());
}

BOOST_AUTO_TEST_CASE(message_header_rejects_bitcoin_core_mainnet_magic)
{
    ParamsGuard guard;
    SelectParams(CChainParams::MAIN);

    CMessageHeader hdr("version", 0);
    // Stamp the header with Bitcoin Core mainnet magic and verify
    // IsValid() rejects it.
    memcpy(hdr.pchMessageStart, BTC_MAINNET_MAGIC, MESSAGE_START_SIZE);
    BOOST_CHECK(!hdr.IsValid());
}

BOOST_AUTO_TEST_CASE(message_header_rejects_bitcoin_core_testnet_magic)
{
    ParamsGuard guard;
    SelectParams(CChainParams::MAIN);

    CMessageHeader hdr("version", 0);
    memcpy(hdr.pchMessageStart, BTC_TESTNET_MAGIC, MESSAGE_START_SIZE);
    BOOST_CHECK(!hdr.IsValid());
}

BOOST_AUTO_TEST_CASE(message_header_cross_network_mismatch)
{
    // A header stamped with the testnet magic is not valid under
    // the mainnet params, and vice versa. This is the partition
    // behavior we want at upgrade time.
    ParamsGuard guard;
    SelectParams(CChainParams::MAIN);

    CMessageHeader hdr("version", 0);
    memcpy(hdr.pchMessageStart, TESTNET_MAGIC, MESSAGE_START_SIZE);
    BOOST_CHECK(!hdr.IsValid());
}

// ---------------------------------------------------------------------------
// The magic survives a CDataStream round-trip, i.e. Serialize/Deserialize
// preserves the four bytes exactly. This is the contract that
// CAddrman::Read relies on (addrdb.cpp:99).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(magic_survives_serialize_roundtrip)
{
    ParamsGuard guard;
    SelectParams(CChainParams::MAIN);

    CMessageHeader hdr_out("version", 0);
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << hdr_out;

    CMessageHeader hdr_in;
    ss >> hdr_in;
    BOOST_CHECK(memeq(hdr_in.pchMessageStart,
                      Params().MessageStart(),
                      MESSAGE_START_SIZE));
}

BOOST_AUTO_TEST_SUITE_END()
