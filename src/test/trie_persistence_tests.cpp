// Copyright (c) 2014 The Mini-Blockchain Project
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Round-trip and corruption-recovery tests for the on-disk trie.dat
// snapshot format. The reader must:
//   - parse the 16-byte header (magic + version + length + CRC32C)
//   - reject unknown magic / unknown version / truncated body /
//     length outside the sanity cap / CRC mismatch
//   - archive the rejected file with a reason-suffixed name so the
//     operator can correlate with debug.log
//   - leave the daemon in an unbuilt state (m_root == nullptr) so
//     slice-sync rebuilds from genesis
// The writer must:
//   - serialize to trie.dat.tmp, fsync, and rename atomically over
//     trie.dat
//   - never leave a stale trie.dat.tmp after a successful Flush
//   - produce a file that round-trips through the reader

#include "main.h"
#include "txdb.h"
#include "trie.h"
#include "trieview.h"

#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <cstdio>
#include <cstring>

BOOST_AUTO_TEST_SUITE(trie_persistence_tests)

namespace fs = boost::filesystem;

// RAII helper that swaps the global datadir to a fresh temp directory
// and restores it on destruction. Every test case that writes files
// under the datadir MUST use this -- mutating mapArgs["-datadir"]
// without restoring it pollutes later tests and the TestingSetup's
// teardown (the same global-state-leak class that previously crashed
// the test binary in the fixture's remove_all).
struct DatadirGuard
{
    std::string prev;
    fs::path    tmp;

    explicit DatadirGuard(const std::string& tag)
    {
        prev = mapArgs["-datadir"];
        tmp  = GetTempPath() / strprintf("trie_persist_%s_%lu",
                                        tag.c_str(),
                                        (unsigned long)GetTime());
        fs::create_directories(tmp);
        mapArgs["-datadir"] = tmp.string();
        // GetDataDir() caches its first computed result and never
        // re-reads mapArgs["-datadir"] until ClearDatadirCache() runs,
        // so swapping the global here would silently be ignored by
        // TrieView's flush path. Drop the cache so the next call sees
        // our new tempdir.
        ClearDatadirCache();
    }

    ~DatadirGuard()
    {
        mapArgs["-datadir"] = prev;
        try { fs::remove_all(tmp); } catch (...) {}
    }
};

// ---------------------------------------------------------------------------
// Round-trip: Force() writes a trie to disk; a fresh TrieView constructor
// must read it back with an equivalent m_root->Hash().
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(roundtrip_force_then_constructor)
{
    DatadirGuard guard("roundtrip");

    // Build a small trie locally and Force() it into disk so we exercise
    // the Flush path end-to-end. Use two distinct accounts so the trie
    // has both a leaf and a branch, not just a single-leaf degenerate.
    TrieView writer;
    uint160 a, b;
    a.SetHex("1111111111111111111111111111111111111111");
    b.SetHex("2222222222222222222222222222222222222222");

    TrieNode* root = nullptr;
    {
        TrieNode* na = new TrieNode(NODE_LEAF);
        na->SetKey(a);
        na->SetBalance(1000);
        na->SetAge(1);
        TrieEngine::Insert(&root, na);
    }
    {
        TrieNode* nb = new TrieNode(NODE_LEAF);
        nb->SetKey(b);
        nb->SetBalance(2000);
        nb->SetAge(2);
        TrieEngine::Insert(&root, nb);
    }
    uint256 block;
    block.SetHex("3333333333333333333333333333333333333333333333333333333333333333");
    writer.Force(root, block);

    // Confirm the file landed at the expected location with the expected
    // magic at offset 0.
    fs::path f = fs::path(mapArgs["-datadir"]) / "trie.dat";
    BOOST_REQUIRE(fs::exists(f));
    FILE* fp = fopen(f.string().c_str(), "rb");
    BOOST_REQUIRE(fp != nullptr);
    unsigned char hdr[4] = {0};
    size_t got = fread(hdr, 1, 4, fp);
    fclose(fp);
    BOOST_REQUIRE_EQUAL(got, 4u);
    BOOST_CHECK_EQUAL(hdr[0], 'X');
    BOOST_CHECK_EQUAL(hdr[1], 'C');
    BOOST_CHECK_EQUAL(hdr[2], 'N');
    BOOST_CHECK_EQUAL(hdr[3], 'A');

    // Now construct a fresh TrieView and verify it loaded a non-null
    // trie whose bestBlock hash matches the one we wrote.
    TrieView reader;
    BOOST_CHECK(reader.GetBestBlock() == block);
    BOOST_CHECK(reader.Accounts() == 2u);
}

// ---------------------------------------------------------------------------
// Corrupt a single payload byte and confirm the reader rejects with the
// "checksum-mismatch" archive reason. This is the most important test:
// it proves the CRC32C catches the partial-write scenario that would
// otherwise silently load a wrong trie.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(corrupt_payload_byte_causes_archive)
{
    DatadirGuard guard("crc");

    // First produce a valid file via Force().
    TrieView writer;
    uint160 a;
    a.SetHex("4444444444444444444444444444444444444444");
    TrieNode* root = nullptr;
    {
        TrieNode* na = new TrieNode(NODE_LEAF);
        na->SetKey(a);
        na->SetBalance(42);
        na->SetAge(7);
        TrieEngine::Insert(&root, na);
    }
    uint256 block;
    block.SetHex("5555555555555555555555555555555555555555555555555555555555555555");
    writer.Force(root, block);

    // Flip one byte in the body to invalidate the CRC. We flip a byte
    // well past the header (the header's own CRC is NOT covered by the
    // CRC32C slot, only the body is -- this matches LevelDB's own log
    // record format) so the test specifically validates the body CRC.
    fs::path f = fs::path(mapArgs["-datadir"]) / "trie.dat";
    {
        FILE* fp = fopen(f.string().c_str(), "r+b");
        BOOST_REQUIRE(fp != nullptr);
        BOOST_REQUIRE_EQUAL(fseek(fp, 20, SEEK_SET), 0); // 16-byte header + 4 bytes into bestBlock
        unsigned char byte = 0;
        BOOST_REQUIRE_EQUAL(fread(&byte, 1, 1, fp), 1u);
        byte ^= 0xA5;
        BOOST_REQUIRE_EQUAL(fseek(fp, 20, SEEK_SET), 0);
        BOOST_REQUIRE_EQUAL(fwrite(&byte, 1, 1, fp), 1u);
        fclose(fp);
    }

    // Reading the corrupted file must produce an unbuilt trie and an
    // archive. The exact archive filename includes a Unix timestamp so
    // we glob the directory for any trie.dat.checksum-mismatch-* file.
    TrieView reader;
    BOOST_CHECK_EQUAL(reader.Accounts(), 0u);

    bool found = false;
    for (fs::directory_iterator it{fs::path(mapArgs["-datadir"])};
         it != fs::directory_iterator();
         ++it) {
        const std::string name = it->path().filename().string();
        if (name.find("trie.dat.checksum-mismatch-") == 0) {
            found = true;
            break;
        }
    }
    BOOST_CHECK(found);
}

// ---------------------------------------------------------------------------
// Foreign magic -> bad-magic archive.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(foreign_magic_causes_archive)
{
    DatadirGuard guard("magic");

    // Write a file whose first 4 bytes are NOT "XCNA". Any plausible
    // 4-byte pattern from a different format works; use the legacy
    // Bitcoin Core "magic" so the test doubles as a regression for an
    // operator accidentally copying a wallet.dat into the datadir.
    fs::path f = fs::path(mapArgs["-datadir"]) / "trie.dat";
    {
        FILE* fp = fopen(f.string().c_str(), "wb");
        BOOST_REQUIRE(fp != nullptr);
        unsigned char hdr[16] = {0xf9, 0xbe, 0xb4, 0xd9, 0,0,0,0, 0,0,0,0, 0,0,0,0};
        BOOST_REQUIRE_EQUAL(fwrite(hdr, 1, sizeof(hdr), fp), sizeof(hdr));
        fclose(fp);
    }

    TrieView reader;
    BOOST_CHECK_EQUAL(reader.Accounts(), 0u);

    bool found = false;
    for (fs::directory_iterator it{fs::path(mapArgs["-datadir"])};
         it != fs::directory_iterator();
         ++it) {
        const std::string name = it->path().filename().string();
        if (name.find("trie.dat.bad-magic-") == 0) {
            found = true;
            break;
        }
    }
    BOOST_CHECK(found);
}

// ---------------------------------------------------------------------------
// Future version -> bad-version archive. This is the forward-compatibility
// gate that lets a future migration ship a v2 file and have older builds
// recognize-and-archive it instead of silently corrupting state.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(unknown_version_causes_archive)
{
    DatadirGuard guard("version");

    fs::path f = fs::path(mapArgs["-datadir"]) / "trie.dat";
    {
        FILE* fp = fopen(f.string().c_str(), "wb");
        BOOST_REQUIRE(fp != nullptr);
        unsigned char hdr[16] = {0};
        memcpy(hdr, "XCNA", 4);
        uint32_t v = 999;
        memcpy(hdr + 4, &v, 4);
        BOOST_REQUIRE_EQUAL(fwrite(hdr, 1, sizeof(hdr), fp), sizeof(hdr));
        fclose(fp);
    }

    TrieView reader;
    BOOST_CHECK_EQUAL(reader.Accounts(), 0u);

    bool found = false;
    for (fs::directory_iterator it{fs::path(mapArgs["-datadir"])};
         it != fs::directory_iterator();
         ++it) {
        const std::string name = it->path().filename().string();
        if (name.find("trie.dat.bad-version-") == 0) {
            found = true;
            break;
        }
    }
    BOOST_CHECK(found);
}

// ---------------------------------------------------------------------------
// Truncated header (too short to even cover the 16-byte fixed header).
// Mirrors the legacy short-read fix from the assert-removal pass but
// at the new granularity: a file whose header itself is incomplete
// must be archived with reason "truncated-header", not silently
// ignored and not aborted.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(truncated_header_causes_archive)
{
    DatadirGuard guard("trunc_hdr");

    fs::path f = fs::path(mapArgs["-datadir"]) / "trie.dat";
    {
        FILE* fp = fopen(f.string().c_str(), "wb");
        BOOST_REQUIRE(fp != nullptr);
        unsigned char hdr[8] = {0}; // half a header
        BOOST_REQUIRE_EQUAL(fwrite(hdr, 1, sizeof(hdr), fp), sizeof(hdr));
        fclose(fp);
    }

    TrieView reader;
    BOOST_CHECK_EQUAL(reader.Accounts(), 0u);

    bool found = false;
    for (fs::directory_iterator it{fs::path(mapArgs["-datadir"])};
         it != fs::directory_iterator();
         ++it) {
        const std::string name = it->path().filename().string();
        if (name.find("trie.dat.truncated-header-") == 0) {
            found = true;
            break;
        }
    }
    BOOST_CHECK(found);
}

// ---------------------------------------------------------------------------
// After a successful Flush, the tmp file must NOT linger. This proves
// the rename step actually ran (and did not get skipped because of a
// fwrite failure). On filesystems where rename is non-atomic (rare,
// but real on some FUSE / network mounts) this test may flake -- we
// skip the strict check in that case and just assert the live file
// exists with the correct magic, which is the user-visible guarantee.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(no_tmp_leftover_on_success)
{
    DatadirGuard guard("tmp_cleanup");

    TrieView writer;
    uint160 a;
    a.SetHex("6666666666666666666666666666666666666666");
    TrieNode* root = nullptr;
    {
        TrieNode* na = new TrieNode(NODE_LEAF);
        na->SetKey(a);
        na->SetBalance(99);
        na->SetAge(3);
        TrieEngine::Insert(&root, na);
    }
    uint256 block;
    block.SetHex("7777777777777777777777777777777777777777777777777777777777777777");
    writer.Force(root, block);

    fs::path live = fs::path(mapArgs["-datadir"]) / "trie.dat";
    fs::path tmp  = fs::path(mapArgs["-datadir"]) / "trie.dat.tmp";

    BOOST_REQUIRE(fs::exists(live));

    // The tmp file is allowed to exist ONLY if it is a stale leftover
    // from a previous Flush that we just overwrote (the constructor
    // discards it on next start). After Force() returns the rename
    // must have happened, so the tmp file MUST NOT be present on any
    // sane filesystem.
    if (fs::exists(tmp)) {
        BOOST_ERROR("trie.dat.tmp still exists after a successful Flush -- rename step did not run");
    }
}

BOOST_AUTO_TEST_SUITE_END()
