// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txdb.h"

#include "core.h"
#include "uint256.h"

#include <stdint.h>

using namespace std;

CBlockTreeDB::CBlockTreeDB(size_t nCacheSize, bool fMemory, bool fWipe) : CLevelDBWrapper(GetDataDir() / "blocks" / "index", nCacheSize, fMemory, fWipe) {
}

bool CBlockTreeDB::WriteBlockIndex(const CDiskBlockIndex& blockindex)
{
    return Write(make_pair('b', blockindex.GetBlockHash()), blockindex);
}

/* bool CBlockTreeDB::WriteBestInvalidWork(const CBigNum& bnBestInvalidWork)
{
    // Obsolete; only written for backward compatibility.
    return Write('I', bnBestInvalidWork);
}
*/ // eliminated for OpenSSL1.1 compatibility

bool CBlockTreeDB::WriteSyncPoint(uint256 p){
    return Write('S', p); 
}

bool CBlockTreeDB::ReadSyncPoint(uint256 &p){
    return Read('S',p);
}

bool CBlockTreeDB::WriteBlockFileInfo(int nFile, const CBlockFileInfo &info) {
    return Write(make_pair('f', nFile), info);
}

bool CBlockTreeDB::ReadBlockFileInfo(int nFile, CBlockFileInfo &info) {
    return Read(make_pair('f', nFile), info);
}

bool CBlockTreeDB::WriteLastBlockFile(int nFile) {
    return Write('l', nFile);
}

bool CBlockTreeDB::WriteReindexing(bool fReindexing) {
    if (fReindexing)
        return Write('R', '1');
    else
        return Erase('R');
}

bool CBlockTreeDB::ReadReindexing(bool &fReindexing) {
    fReindexing = Exists('R');
    return true;
}

bool CBlockTreeDB::ReadLastBlockFile(int &nFile) {
    return Read('l', nFile);
}

bool CBlockTreeDB::ReadTxIndex(const uint256 &txid, CDiskTxPos &pos) {
    return Read(make_pair('t', txid), pos);
}

bool CBlockTreeDB::WriteTxIndex(const std::vector<std::pair<uint256, CDiskTxPos> >&vect) {
    CLevelDBBatch batch;
    for (std::vector<std::pair<uint256,CDiskTxPos> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(make_pair('t', it->first), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::EraseTxIndex(uint256 key) {
    return Erase(make_pair('t',key));
}


bool CBlockTreeDB::WriteFlag(const std::string &name, bool fValue) {
    return Write(std::make_pair('F', name), fValue ? '1' : '0');
}

bool CBlockTreeDB::ReadFlag(const std::string &name, bool &fValue) {
    char ch;
    if (!Read(std::make_pair('F', name), ch))
        return false;
    fValue = ch == '1';
    return true;
}

bool CBlockTreeDB::LoadBlockIndexGuts() {
    leveldb::Iterator *pcursor = NewIterator();
    CDataStream ssKeySet(SER_DISK, CLIENT_VERSION), ssKey(SER_DISK, CLIENT_VERSION), ssValue(SER_DISK, CLIENT_VERSION);
    ssKeySet << std::make_pair('b', uint256(0));
    pcursor->Seek(ssKeySet.str());

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        
        // Reusing the streams by clearing them for new data
        ssKey.clear();
        ssValue.clear();
        
        leveldb::Slice slKey = pcursor->key();
        leveldb::Slice slValue = pcursor->value();
        
        ssKey.reserve(slKey.size());
        ssKey.write(slKey.data(), slKey.size());
        char chType;
        ssKey >> chType;

        if (chType == 'b') {
            ssValue.reserve(slValue.size());
            ssValue.write(slValue.data(), slValue.size());
            CDiskBlockIndex diskindex;

            try {
                ssValue >> diskindex;
            } catch (std::exception &e) {
                delete pcursor;
                return error("%s : Deserialize or I/O error - %s", __PRETTY_FUNCTION__, e.what());
            }

            if (!diskindex.CheckIndex()) {
                error("LoadBlockIndex() : CheckIndex failed: %s", diskindex.GetBlockHash().ToString().c_str());
                pcursor->Next();
                continue;
            }

            CBlockIndex* pindexNew = InsertBlockIndex(diskindex.GetBlockHash(), diskindex.GetBlockHeader());
            if (pindexNew) { // Check for null pointer
                pindexNew->nFile = diskindex.nFile;
                pindexNew->nDataPos = diskindex.nDataPos;
                pindexNew->nUndoPos = diskindex.nUndoPos;
                pindexNew->nStatus = diskindex.nStatus;
                pindexNew->nTx = diskindex.nTx;
            }
        } else {
            break; // Finished loading block index
        }
        pcursor->Next();
    }
    
    delete pcursor; // Always free resources
    return true;
}
