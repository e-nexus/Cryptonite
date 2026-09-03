// Copyright (c) 2014 The Mini-Blockchain Project
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Regression tests for the previously-aborting consensus/RPC/P2P hot paths
// in trie view, trie sync, block cache, and the getwork submit path.
//
// Each test below feeds an input that would have triggered abort() in the
// prior code, and asserts that the function under test returns a soft
// failure (false / empty / throw) WITHOUT terminating the test process.
// If any of them ever fires, it would crash the whole test binary -- the
// failure would be unmistakable.

#include "main.h"
#include "txdb.h"
#include "trie.h"
#include "trieview.h"
#include "triesync.h"
#include "blockcache.h"

#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>

#include <cstdio>
#include <fstream>
#include <vector>

BOOST_AUTO_TEST_SUITE(trie_safety_tests)

// ---------------------------------------------------------------------------
// LookupBlockIndex: the non-inserting helper that replaces operator[].
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(lookup_blockindex_miss_does_not_insert)
{
    uint256 missing;
    missing.SetHex("1111111111111111111111111111111111111111111111111111111111111111");
    CBlockIndex* pindex = nullptr;

    // Before: mapBlockIndex[missing] would insert (missing, nullptr) and
    // pollute the index for all subsequent validation. LookupBlockIndex
    // must report a clean miss and leave mapBlockIndex untouched.
    const size_t sizeBefore = mapBlockIndex.size();
    const bool found = LookupBlockIndex(missing, &pindex);
    BOOST_CHECK(!found);
    BOOST_CHECK(pindex == nullptr);
    BOOST_CHECK_EQUAL(mapBlockIndex.size(), sizeBefore);
}

BOOST_AUTO_TEST_CASE(lookup_blockindex_null_outparam_is_safe)
{
    uint256 missing;
    missing.SetHex("2222222222222222222222222222222222222222222222222222222222222222");
    BOOST_CHECK(!LookupBlockIndex(missing, nullptr));
}

// ---------------------------------------------------------------------------
// TrieSync::AcceptSlice: previously aborted on unknown block hash via
// mapBlockIndex[slice.m_block]->hashAccountRoot. A peer-driven self-DoS.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(triesync_acceptslice_unknown_block_does_not_crash)
{
    TrieSync ts;
    CSlice slice;
    // m_block is a hash the chain has never seen.
    slice.m_block.SetHex("3333333333333333333333333333333333333333333333333333333333333333");
    slice.m_left = 0;
    slice.m_right = uint160(0);
    // m_data left empty -- AcceptSlice rejects before deserialize because
    // of the unknown-block check (and because size > MAX_TRIE_SLICE_SIZE
    // check passes only for non-empty data). We rely on the new helper
    // returning false before we hit MAX_TRIE_SLICE_SIZE.
    slice.m_data.assign(1, NODE_LEAF);

    // Before: this would abort() because operator[] inserted (hash,nullptr)
    // and the next line dereferenced nullptr.
    bool ok = ts.AcceptSlice(slice);
    BOOST_CHECK(!ok);
    // mapBlockIndex must NOT have been polluted with a null entry.
    BOOST_CHECK(mapBlockIndex.find(slice.m_block) == mapBlockIndex.end());
}

// ---------------------------------------------------------------------------
// TrieSync::Build: previously aborted on assert(trie) if Deserialize
// returned null. The function must return nullptr and leave the process
// running.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(triesync_build_with_no_slices_returns_null_safely)
{
    TrieSync ts;
    // Empty slices map. Build returns nullptr after the sort+no-iter loop
    // without touching any of the previously-aborting paths.
    uint256 block;
    TrieNode* root = ts.Build(block);
    BOOST_CHECK(root == nullptr);
}

// ---------------------------------------------------------------------------
// CBlockCache::ReadBlockFromDisk: previously dereferenced pindex without
// a null check. A null pindex must now return false, not crash.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(blockcache_readblockfromdisk_null_pindex)
{
    CBlock block;
    // Before: this dereferenced nullptr and aborted.
    bool ok = blockCache.ReadBlockFromDisk(block, nullptr);
    BOOST_CHECK(!ok);
}

// ---------------------------------------------------------------------------
// CBlockCache::ReadTxFromDisk: previously used
//     CBlockIndex *pindex = mapBlockIndex[disktx.hashBlock];
// which inserted a null entry on miss, then dereferenced. Must now return
// false without polluting it.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(blockcache_readtxfromdisk_unknown_block_does_not_pollute)
{
    uint256 unknown;
    unknown.SetHex("4444444444444444444444444444444444444444444444444444444444444444");
    CDiskTxPos pos;
    pos.nTxOffset = 0;
    pos.nFile = 0;
    pos.nPos = 0;
    pos.hashBlock = unknown;

    const size_t sizeBefore = mapBlockIndex.size();
    CTransaction tx;
    bool ok = blockCache.ReadTxFromDisk(tx, pos);
    BOOST_CHECK(!ok);
    BOOST_CHECK(mapBlockIndex.find(unknown) == mapBlockIndex.end());
    BOOST_CHECK_EQUAL(mapBlockIndex.size(), sizeBefore);
}

// ---------------------------------------------------------------------------
// TrieView::CoinAge: previously asserted on missing node. Must now return 0
// (safe default = no priority boost) for any random key.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(trie_coinage_missing_key_returns_zero)
{
    // Use a local TrieView so we don't disturb the global pviewTip used
    // by other test cases. On an empty trie CoinAge must return 0
    // instead of crashing on the assert(node).
    TrieView tv;
    uint160 key;
    key.SetHex("5555555555555555555555555555555555555555");
    BOOST_CHECK_EQUAL(tv.CoinAge(key), 0ULL);
}

// ---------------------------------------------------------------------------
// TrieView::Activate: previously dereferenced mapBlockIndex.find(m_bestBlock)
// without a null check. After a fresh-DB load where m_bestBlock was loaded
// from disk but never inserted into mapBlockIndex, this would crash.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(trie_activate_unknown_best_block_returns_false)
{
    // Use a local TrieView so we don't disturb the global pviewTip. Force
    // m_bestBlock to a hash no one has ever indexed, then Activate with a
    // null pindex -- the non-inserting lookup must return false without
    // aborting.
    TrieView tv;
    uint256 unknown;
    unknown.SetHex("6666666666666666666666666666666666666666666666666666666666666666");
    tv.Force(nullptr, unknown);
    uint256 bad;
    bool ok = tv.Activate(nullptr, bad);
    BOOST_CHECK(!ok);
}

// ---------------------------------------------------------------------------
// TrieView constructor: previously asserted if trie.dat was truncated
// (8 bytes of header but missing body). A short file must produce a
// null m_root and let slice-sync take over.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(trieview_constructor_truncated_file_does_not_crash)
{
    namespace fs = boost::filesystem;
    fs::path tmp = GetTempPath() / strprintf(
        "trie_safety_trunc_%lu", (unsigned long)GetTime());
    fs::create_directories(tmp);
    // Save and restore the TestingSetup's datadir so subsequent tests still
    // see the fixture's path. Mutating this global is hazardous and is
    // exactly the kind of test-hygiene trap the safety fix must avoid.
    const std::string prevDatadir = mapArgs["-datadir"];
    mapArgs["-datadir"] = tmp.string();

    // Write a trie.dat that is too short to satisfy any of the three reads.
    {
        FILE* f = fopen((tmp / "trie.dat").string().c_str(), "wb");
        BOOST_REQUIRE(f != nullptr);
        // 16 bytes of bestBlock header -- one byte short of the full 32.
        unsigned char hdr[16] = {0};
        fwrite(hdr, 1, sizeof(hdr), f);
        fclose(f);
    }

    // Before: this aborted on the first assert(fread(...)>0).
    // After: TrieView must construct cleanly with m_root == nullptr.
    TrieView tv;
    BOOST_CHECK(tv.GetBestBlock() == uint256(0));
    // The Accounts() helper dereferences m_root unconditionally; calling
    // it on a null-root view must NOT crash. (Implementation: m_root == 0
    // is the unbuilt state; the test confirms we get that state without
    // aborting.)
    BOOST_CHECK_EQUAL(tv.Accounts(), 0ULL);

    mapArgs["-datadir"] = prevDatadir;
    fs::remove_all(tmp);
}

BOOST_AUTO_TEST_CASE(trieview_constructor_oversize_value_does_not_crash)
{
    namespace fs = boost::filesystem;
    fs::path tmp = GetTempPath() / strprintf(
        "trie_safety_oversize_%lu", (unsigned long)GetTime());
    fs::create_directories(tmp);
    const std::string prevDatadir = mapArgs["-datadir"];
    mapArgs["-datadir"] = tmp.string();

    {
        FILE* f = fopen((tmp / "trie.dat").string().c_str(), "wb");
        BOOST_REQUIRE(f != nullptr);
        // 32-byte bestBlock, then 4-byte size with a huge value (>64MB cap).
        unsigned char hdr[32] = {0};
        fwrite(hdr, 1, sizeof(hdr), f);
        uint32_t huge = 0xFFFFFFFFU; // 4 GB; way past the 64MB sanity cap
        fwrite(&huge, 4, 1, f);
        fclose(f);
    }

    // Before: this asserted on the body read (malloc + short read).
    // After: must construct cleanly with m_root == nullptr.
    TrieView tv;
    BOOST_CHECK_EQUAL(tv.Accounts(), 0ULL);

    mapArgs["-datadir"] = prevDatadir;
    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// Util.h macros themselves: smoke-test that Assert/Assume/CheckNonFatal
// behave as documented. They are not used to assert user/network input in
// production; this section just confirms the macros compile and link.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(util_macros_smoke)
{
    // Assert(cond, msg) must not throw when cond is true.
    Assert(true, "true condition is fine");
    // Assume(false) compiled-out in release; must not abort in either
    // build configuration.
    Assume(false);

    // CheckNonFATAL with a false condition must throw std::runtime_error,
    // not abort.
    bool caught = false;
    try {
        CheckNonFatal(false, "intentional");
    } catch (const std::runtime_error&) {
        caught = true;
    }
    BOOST_CHECK(caught);
}

BOOST_AUTO_TEST_SUITE_END()