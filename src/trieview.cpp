// Copyright (c) 2014 The Mini-Blockchain Project
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "txdb.h"
#include "init.h"
#ifdef ENABLE_WALLET
#include "wallet.h"
#endif

#include <boost/filesystem/exception.hpp>
#include <boost/filesystem/operations.hpp>
#include <cerrno>
#include <cstring>
#include <leveldb/crc32c.h>

extern map<uint256, CBlockIndex*> mapBlockIndex;

CCriticalSection cs_trie;

namespace {

// On-disk format for the account trie snapshot.
//
// Layout (little-endian, no padding):
//   offset 0   : 4 bytes  magic           = "XCNA"
//   offset 4   : 4 bytes  format version  = 1
//   offset 8   : 4 bytes  payload length  (uint32)
//   offset 12  : 4 bytes  CRC32C of (bestBlock || payload), masked per LevelDB convention
//   offset 16  : 32 bytes bestBlock        (uint256, big-endian wire form)
//   offset 48  : N bytes  payload          (serialized trie via TrieEngine::SubTrie)
//
// Atomicity is provided by writing the new file to trie.dat.tmp, fsyncing,
// and renaming over trie.dat. The CRC32C lets the reader detect partial
// writes from crashes that interrupted the tmp file before the rename.
//
// CRC32C (Castagnoli) is preferred over CRC32 (Ethernet) because every
// modern x86_64 / arm64 CPU ships a hardware implementation via SSE4.2 /
// CRC32 instructions, the polynomial has better error-detection
// properties for short messages, and it is the same primitive LevelDB
// uses internally for its own log records and SST blocks. The vendored
// implementation in src/leveldb/util/crc32c.{h,cc} is already linked into
// the binary via libleveldb.a, so this costs zero new dependencies.
const char        TRIE_FILE_MAGIC[4]   = { 'X', 'C', 'N', 'A' };
const uint32_t    TRIE_FILE_VERSION    = 1;
const size_t      TRIE_FILE_HEADER_LEN = 16;
const size_t      TRIE_BESTBLOCK_LEN   = 32;
const char*       TRIE_FILE_TMP_SUFFIX = ".tmp";

// Move a file we have decided not to trust out of the way so the next
// start does not re-trigger the same failure. The suffix records the
// reason we rejected the file so an operator triaging debug.log can
// correlate. Throws on filesystem errors; the caller is responsible for
// wrapping in a try/catch if it cannot tolerate the original file
// being left in place. In practice the call sites are best-effort
// paths and the daemon must come up regardless, so they catch.
void ArchiveTrieFile(const boost::filesystem::path& src, const std::string& reason)
{
    namespace fs = boost::filesystem;
    fs::path archive = src.parent_path() / (src.filename().string() + "." + reason + "-" + std::to_string(GetTime()));
    fs::rename(src, archive);
    LogPrintf("WARNING: archived %s -> %s (%s)\n", src.string().c_str(), archive.string().c_str(), reason.c_str());
}

} // anonymous namespace

bool LookupBlockIndex(const uint256& hash, CBlockIndex** ppindex)
{
    if (ppindex == nullptr) return false;
    auto it = mapBlockIndex.find(hash);
    if (it == mapBlockIndex.end() || it->second == nullptr) {
        *ppindex = nullptr;
        return false;
    }
    *ppindex = it->second;
    return true;
}

TrieView::TrieView(){
    m_bestBlock = 0;
    m_root = 0;

    boost::filesystem::path pathDebug = GetDataDir() / "trie.dat";

    // A tmp file from a previous crashed write is safe to ignore: the
    // next successful Flush will overwrite it, and on a clean start we
    // just tidy up so the datadir stays clean. Throws on filesystem
    // errors; we catch and log because the daemon must come up even if
    // the datadir is partially inaccessible.
    {
        boost::filesystem::path pathTmp = pathDebug;
        pathTmp += TRIE_FILE_TMP_SUFFIX;
        try {
            if (boost::filesystem::remove(pathTmp))
                LogPrintf("Discarded stale trie.dat.tmp from a previous unclean shutdown\n");
        } catch (const boost::filesystem::filesystem_error& e) {
            LogPrintf("WARNING: could not remove stale trie.dat.tmp (%s)\n", e.what());
        }
    }
    FILE* filein = fopen(pathDebug.string().c_str(), "rb");
    if (!filein)
        return; // first run or freshly-archived -- caller rebuilds via slice-sync

    printf("Opening %s\n", pathDebug.string().c_str());

    // fileOpen tracks whether filein still holds an open stdio FILE so
    // every code path closes it at most once. The fail lambda below is
    // reused by both the early-exit branches (where filein is still
    // open) and the post-read branches (where the long-body read has
    // already closed it); without the guard, a corrupted-but-fully-
    // read body would close the FILE twice and glibc aborts with
    // "free(): double free detected" inside the second fclose.
    bool fileOpen = true;

    // Helper: any failure path from here on must close filein (if it
    // is still open), archive the file with a reason tag, and leave
    // m_root/m_bestBlock at the "unbuilt" defaults so slice-sync can
    // rebuild from genesis. Archive is wrapped in try/catch: the
    // daemon must come up even if the rename fails (e.g. permission
    // denied on the datadir), so we log and continue instead of
    // propagating the exception.
    auto fail = [&](const char* reason, const char* detail) {
        LogPrintf("WARNING: trie.dat rejected (%s%s%s); archiving and falling back to slice-sync\n",
                  reason,
                  detail ? ": " : "",
                  detail ? detail : "");
        if (fileOpen) {
            fclose(filein);
            fileOpen = false;
        }
        try {
            ArchiveTrieFile(pathDebug, reason);
        } catch (const boost::filesystem::filesystem_error& e) {
            LogPrintf("WARNING: could not archive %s (%s); leaving in place\n",
                      pathDebug.string().c_str(), e.what());
        }
    };

    // Read fixed-size header.
    unsigned char hdr[TRIE_FILE_HEADER_LEN];
    size_t got = fread(hdr, 1, TRIE_FILE_HEADER_LEN, filein);
    if (got != TRIE_FILE_HEADER_LEN) {
        fail("truncated-header", nullptr);
        return;
    }

    // Magic check -- separates "our format" from anything else that
    // happens to be in the file (random data, a future fork, an older
    // unversioned build, a copy-pasted wallet.dat, etc.).
    if (memcmp(hdr, TRIE_FILE_MAGIC, 4) != 0) {
        char want[5]; memcpy(want, TRIE_FILE_MAGIC, 4); want[4] = 0;
        char gotstr[5]; memcpy(gotstr, hdr, 4); gotstr[4] = 0;
        char detail[64];
        snprintf(detail, sizeof(detail), "got \"%s\", want \"%s\"", gotstr, want);
        fail("bad-magic", detail);
        return;
    }

    uint32_t version;
    memcpy(&version, hdr + 4, 4);
    if (version != TRIE_FILE_VERSION) {
        char detail[32];
        snprintf(detail, sizeof(detail), "got v%u", version);
        fail("bad-version", detail);
        return;
    }

    uint32_t payload_len;
    memcpy(&payload_len, hdr + 8, 4);

    // Same sanity cap as the legacy reader: a real trie is well under
    // 64MB; an absurd value is corruption or an attacker-crafted file.
    if (payload_len == 0 || payload_len > 64u * 1024u * 1024u) {
        char detail[32];
        snprintf(detail, sizeof(detail), "len=%u", payload_len);
        fail("bad-length", detail);
        return;
    }

    uint32_t stored_crc;
    memcpy(&stored_crc, hdr + 12, 4);

    // Read bestBlock + payload into a single contiguous buffer so the
    // CRC32C can cover them in one pass.
    const size_t body_len = TRIE_BESTBLOCK_LEN + payload_len;
    unsigned char* body = (unsigned char*)malloc(body_len);
    if (!body) {
        fail("oom", nullptr);
        return;
    }
    got = fread(body, 1, body_len, filein);
    if (fileOpen) {
        fclose(filein);
        fileOpen = false;
    }
    if (got != body_len) {
        char detail[64];
        snprintf(detail, sizeof(detail), "got %zu of %zu bytes", got, body_len);
        free(body);
        fail("truncated-body", detail);
        return;
    }

    // CRC32C verification -- catches partial writes, bit-rot, and any
    // non-crash corruption. CRC32C over (bestBlock || payload) keeps
    // the two fields coupled: an attacker who can flip one cannot flip
    // the other without invalidating the checksum.
    uint32_t actual_crc = leveldb::crc32c::Value(reinterpret_cast<const char*>(body), body_len);
    if (leveldb::crc32c::Unmask(stored_crc) != actual_crc) {
        free(body);
        fail("checksum-mismatch", nullptr);
        return;
    }

    // Extract bestBlock and deserialize the trie payload. From here on
    // any failure is a content-level problem with the CRC-validated
    // bytes; we have already proven the file is intact, so a Deserialize
    // failure means the trie engine changed shape since the file was
    // written -- worth knowing about, but the daemon must still come up.
    uint256 bestBlock;
    memcpy(&bestBlock, body, TRIE_BESTBLOCK_LEN);
    TrieNode* root = TrieNode::Deserialize(body + TRIE_BESTBLOCK_LEN, payload_len);
    free(body);

    if (root == nullptr) {
        fail("deserialize-failed", nullptr);
        return;
    }

    m_bestBlock = bestBlock;
    m_root = root;
    LogPrintf("Loaded trie at %s (%u bytes)\n", m_bestBlock.GetHex().c_str(), payload_len);
}

void TrieView::Force(TrieNode *root, uint256 block){
    if(m_root)
	delete m_root;
    m_root = root;
    m_bestBlock = block;
    Flush();
}

static void getSet(CBlockIndex *pindex, set<CBlockIndex*> &theSet){
	while(pindex){
		theSet.insert(pindex);
//                printf("getset %llu\n",pindex->nHeight);

		pindex = pindex->pprev;
	}
}

//#define MAX_BLOCK (1024*1024*2)
#define SAFE_UNIQ 10
#define MORE_ALLOC 1024

static void getSet2(CBlockIndex *pindex, set<CBlockIndex*> &theSet, CBlockIndex *pindex2, set<CBlockIndex*> &theSet2){
	CBlockIndex **p;
	CBlockIndex **p2;
	size_t height = 0;
	if(pindex)
		height = pindex->nHeight;
	if(pindex2 && pindex2->nHeight > height)
		height = pindex2->nHeight; 

	printf("height = %lu\n",height);
	height+=MORE_ALLOC;
 
	p = (CBlockIndex**)malloc((sizeof(CBlockIndex *)) * height);
	p2 = (CBlockIndex**)malloc((sizeof(CBlockIndex *)) * height);
	size_t index=0;
	size_t index2=0;
	        while(pindex){
		p[index]=pindex;
		index++;
                pindex = pindex->pprev;
        }

	        while(pindex2){
                p2[index2]=pindex2;
                index2++;
                pindex2 = pindex2->pprev;
        }
	printf("Added %lu %lu\n",index2,index);
//	printf("%x %x\n",p[index],p2[index2],p2[index2-1]);

	size_t i;
	while (index > SAFE_UNIQ && index2 > SAFE_UNIQ && (p[index-1] == p2[index2-1])){index--;index2--;}
	printf("Reducted %lu %lu\n",index2,index);

	for(i=0;i<index;i++){
		//printf("Inserting %x[%i]\n",p[i],i);
		theSet.insert(p[i]);
}

        for(i=0;i<index2;i++)
                theSet2.insert(p2[i]);
	free(p);
	free(p2);
}

static void sortSet(set<CBlockIndex*> &theSet, vector<pair<uint64_t,CBlockIndex*> > &theVector){
	theVector.reserve(theSet.size());
	set<CBlockIndex*>::iterator it;
	for(it=theSet.begin(); it!=theSet.end(); it++){
		theVector.push_back(make_pair((*it)->nHeight,*it));
	}
	sort(theVector.begin(),theVector.end());
}

bool TrieView::Activate(CBlockIndex* pindex, uint256 &badBlock){
    LOCK(cs_main);
    if (pindex == nullptr) {
        // A null pindex is a caller bug; surface as a soft activation
        // failure rather than aborting inside getSet2()/pprev-walks.
        LogPrintf("Activate: null pindex, refusing\n");
        badBlock = 0;
        return false;
    }
    //Find shortest path to the validated Trie
    set<CBlockIndex*> newSet, oldSet, oldSetCopy;

    // Non-inserting lookup: missing bestBlock means the chain was wiped or
    // a fresh DB started with a stale trie.dat. Treat as activation failure
    // and let the slice-sync engine rebuild from genesis.
    CBlockIndex* pBestIndex = nullptr;
    if (!LookupBlockIndex(m_bestBlock, &pBestIndex)) {
        LogPrintf("Activate: bestBlock %s not in mapBlockIndex, falling back\n",
                  m_bestBlock.GetHex().c_str());
        badBlock = m_bestBlock;
        return false;
    }
    getSet2(pindex,newSet,pBestIndex,oldSet);
//    getSet(pindex,newSet);
//    getSet((*mapBlockIndex.find(m_bestBlock)).second,oldSet);

    LogPrintf("Activate %s\n", pindex->GetBlockHeader().GetHash().GetHex().c_str());

    //TODO: it is probably possible to use CChain to do most of this work

    //we need the sets ordered by height or else it will be impossible to apply
    //in the correct order
 
    printf("OS %ld NS %ld\n", oldSet.size(), newSet.size());

    oldSetCopy = oldSet;
    set<CBlockIndex*>::iterator it;
    for(it = newSet.begin(); it != newSet.end(); it++){
//	printf("olderase %llu\n",(*it)->nHeight);
        oldSet.erase(*it);
    }

    for(it = oldSetCopy.begin(); it != oldSetCopy.end(); it++){
	newSet.erase(*it);
    }

   printf("OSe %ld NSe %ld\n", oldSet.size(), newSet.size());
    
    //TODO: if the sets are longer than cycle time we have detected a pre cycle fork here. 
    //Supposed to break or whatever

    //make sure we have transaction data for the sets or else it must be requested
    //before activation is possible (including invertible data) - situation impossible

    vector<pair<uint64_t,CBlockIndex*> > newVector, oldVector;
    sortSet(oldSet,oldVector);
    sortSet(newSet,newVector);
    reverse(oldVector.begin(), oldVector.end());
	
    vector<pair<uint64_t,CBlockIndex*> >::iterator it2;
    for(it2 = oldVector.begin(); it2 != oldVector.end(); it2++){
    	CBlockUndo blockUndo;
    	CDiskBlockPos pos = (*it2).second->GetUndoPos();
    	if (pos.IsNull())
            return error("DisconnectBlock() : no undo pos vailable");
    	if (!blockUndo.ReadFromDisk(pos, (*it2).second->GetBlockHash()))
            return error("DisconnectBlock() : failure reading undo data");

	list<CTxUndo> undos;
	for(vector<CTxUndo>::iterator it3=blockUndo.vtxundo.begin(); it3!=blockUndo.vtxundo.end(); it3++){
	    undos.push_back(*it3);
	}
	Unapply(undos);
	m_bestBlock = it2->second->GetBlockHash();
    }    

    for(it2 = newVector.begin(); it2 != newVector.end(); it2++){
        //if for some reason we have a failure, we need to unwind all previously 
        //completed actions before return
	if(!Apply((*it2).second)){
		badBlock = (*it2).second->GetBlockHash();
		return false;	
	}
	m_bestBlock = it2->second->GetBlockHash();		
    }
    m_bestBlock = pindex->GetBlockHeader().GetHash();
    return true;
}

#define MIN_BALANCE 1

bool TrieView::TempApply(CBlock block, list<CTxUndo> &undos){
    map<uint160,uint64_t> limits;
    set<uint160> setLimit, setTxIn;

    for (const CTransaction &tx : block.vtx){
	if(tx.IsCoinBase()){
	    TrieNode* node = TrieEngine::Find(0, m_root);
	    uint64_t coinb=0;
	    if(node)
		coinb = node->Balance();  
    	    if (tx.vout[0].nValue > (uint64_t)GetBlockValue(coinb, block.GetFees())){
		LogPrintf("Coinbase paid too much!\n");
		return false;
	    }
	}


	for (const CTxIn& txin : tx.vin){
	    TrieNode* node = TrieEngine::Find(txin.pubKey, m_root);
	    if(!node){
		LogPrintf("Failed to find node for %s\n", txin.pubKey.GetHex().c_str());
		return false;
	    }
	    CTxUndo undo(node->Key());
	    undo.m_balance = node->Balance();
	    undo.m_age = node->Age();
	    undo.m_limit = node->Limit();
	    undo.m_futurelimit = node->FutureLimit();

	    if(!tx.fSetLimit){  //Fee on set limit is allowed to surpass limit field to prevent stuck accounts
		if(limits.find(node->Key()) == limits.end()){
		    if(txin.nValue > node->Limit()){
			LogPrintf("Tried to spend past limit %s %ld %ld\n", txin.pubKey.GetHex().c_str(), txin.nValue, node->Limit());
			return false;
		    }
		    limits[node->Key()] = node->Limit() - txin.nValue;
		}else{
		    if(txin.nValue > limits[node->Key()]){
			LogPrintf("Tried to spend past limit %s %ld %ld\n", txin.pubKey.GetHex().c_str(), txin.nValue, node->Limit());
			return false;
		    }		 
		    limits[node->Key()] -= txin.nValue;
		}
	    }

	    if(txin.nValue > node->Balance()){
		LogPrintf("Source has insufficient balance %s %ld %ld\n", txin.pubKey.GetHex().c_str(), txin.nValue, node->Balance());
		//m_root->Print();
		return false;
	    }

	    if(!tx.fSetLimit && setLimit.count(txin.pubKey)){
		LogPrintf("Limit switch and transaction in same block\n");
		return false;	
	    }

	    if(tx.fSetLimit && tx.nLimitValue < node->Limit() && setTxIn.count(txin.pubKey)){
		LogPrintf("Limit switch and transaction in same block\n");
		return false;
	    }

	    if(block.nHeight - node->Age() > MIN_LIMIT_TIME){
		if(node->FutureLimit() != node->Limit() && setTxIn.count(txin.pubKey)){
		    LogPrintf("Limit switch and transaction in same block\n");
		    return false;
		}
 	        node->SetLimit(node->FutureLimit());		
	    }

	    //No node updates until tx guaranteed to succeed!!!
	    if(tx.fSetLimit){
		node->SetFutureLimit(tx.nLimitValue);
		//Instant update if limit is lower
		if(node->FutureLimit() < node->Limit())
		    node->SetLimit(node->FutureLimit());

		setLimit.insert(txin.pubKey);
	    }else{
		setTxIn.insert(txin.pubKey);
	    }
	    
	    node->SetAge(block.nHeight);
		    
	    node->SetBalance(node->Balance() - txin.nValue);
	    if(node->Balance() < MIN_BALANCE){
		TrieEngine::Remove(&m_root,node);
		undo.m_destroy=true;
	    }
	    undos.push_back(undo);
	}

        for (const CTxOut& txout : tx.vout){
	    TrieNode* node = TrieEngine::Find(txout.pubKey, m_root);
	    if(node){
		CTxUndo undo(node->Key());
		undo.m_balance = node->Balance();
		undo.m_age = node->Age();
		undo.m_limit = node->Limit();
		undo.m_futurelimit = node->FutureLimit();
		undos.push_back(undo);
		//node->SetAge(block.nHeight); //No age update on deposit
		node->SetBalance(node->Balance() + txout.nValue);
	    }else{
		node = new TrieNode(NODE_LEAF);
		node->SetKey(txout.pubKey);
		node->SetAge(block.nHeight);
		node->SetBalance(node->Balance() + txout.nValue);
		TrieEngine::Insert(&m_root,node);
		CTxUndo undo(node->Key());
		undo.m_create = true;
		undos.push_back(undo);
	    }
        }
    }
    reverse(undos.begin(),undos.end());
    return true;
}

bool TrieView::Unapply(list<CTxUndo> &undos){
    list<CTxUndo>::iterator it;
    for(it=undos.begin(); it!= undos.end(); it++){
	CTxUndo undo = *it;
	if(undo.m_create){
	    TrieNode *node = TrieEngine::Find(undo.m_key, m_root);
	    // Corrupted or inconsistent undo data must not abort(); a failed
	    // unapply leaves the trie in an internally consistent (if older)
	    // state -- which is what the surrounding caller already tolerates
	    // via Activate() returning false.
	    if (node == nullptr) {
	        LogPrintf("Unapply: undo references missing node %s, skipping\n",
	                  undo.m_key.GetHex().c_str());
	        continue;
	    }
	    TrieEngine::Remove(&m_root,node);
	    //delete node;
	}else if(undo.m_destroy){
	    TrieNode *node = new TrieNode(NODE_LEAF);
	    node->SetAge(undo.m_age);
	    node->SetKey(undo.m_key);
	    node->SetBalance(undo.m_balance);
	    node->SetLimit(undo.m_limit);
	    node->SetFutureLimit(undo.m_futurelimit);
	    TrieEngine::Insert(&m_root,node);
	}else{
	    TrieNode *node = TrieEngine::Find(undo.m_key, m_root);
	    node->SetAge(undo.m_age);
	    node->SetKey(undo.m_key);
	    node->SetBalance(undo.m_balance);
	    node->SetLimit(undo.m_limit);
	    node->SetFutureLimit(undo.m_futurelimit);
	}
    }
    return true;
}

bool TrieView::HashForBlock(CBlock block, uint256 &hash){
    LOCK(cs_main);

    if(m_bestBlock != block.hashPrevBlock){
	LogPrintf("HashForBlock(): m_bestBlock hashPrevBlock mismatch!");
	return false;
    }
    list<CTxUndo> undos;
    if(!TempApply(block,undos)){
	Unapply(undos);
	return false;
    }   
    hash = m_root->Hash();
    Unapply(undos);
    return true;
}

uint64_t TrieView::Accounts(){
    if (m_root == nullptr) return 0;
    return m_root->Children();
}

void backtrace();

//Apply the tx's in pindex to the trie. This assume the current state of the trie is pindex->pprev. This code will also generate the undo
//block and save it to the trie if possible. Pindex can be invalid!. This code will detect and unwind an invalid tx set. 
bool TrieView::Apply(CBlockIndex *pindex){
    CBlock block;
    if (!blockCache.ReadBlockFromDisk(block, pindex)) {
	    //printf("WTF: %s %s\n", pindex->hashAccountRoot.GetHex().c_str(), block.hashAccountRoot.GetHex().c_str());
	    //backtrace();
	    return false;
    }


    //we must generate invertible data of the block at this point
    //otherwise trie can not be unwound
    list<CTxUndo> undos;
    if(!TempApply(block,undos)){
	LogPrintf("Could not apply tx's!");
	Unapply(undos);
	return false;
    }

    if(m_root->Hash()!=block.hashAccountRoot){
	LogPrintf("Master hash mismatch: %s %s %s\n", pindex->GetBlockHash().GetHex().c_str(),
		m_root->Hash().GetHex().c_str(), block.hashAccountRoot.GetHex().c_str());
	Unapply(undos);
	return false;
    }

    // Write undo information to disk
    CBlockUndo blockundo;
    for(list<CTxUndo>::iterator it = undos.begin(); it!= undos.end(); it++)
	blockundo.vtxundo.push_back(*it);
    if (pindex->GetUndoPos().IsNull() || (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_SCRIPTS)
    {
        if (pindex->GetUndoPos().IsNull()) {
            CDiskBlockPos pos;
	    CValidationState state;
            if (!FindUndoPos(state, pindex->nFile, pos, ::GetSerializeSize(blockundo, SER_DISK, CLIENT_VERSION) + 40))
                return error("ConnectBlock() : FindUndoPos failed");
            if (!blockundo.WriteToDisk(pos, block.GetBlockHeader().GetHash()))
                return error("Failed to write undo data");

            // update nUndoPos in block index
            pindex->nUndoPos = pos.nPos;
            pindex->nStatus |= BLOCK_HAVE_UNDO;
        }

        pindex->nStatus = (pindex->nStatus & ~BLOCK_VALID_MASK) | BLOCK_VALID_SCRIPTS;

        CDiskBlockIndex blockindex(pindex);
        if (!pblocktree->WriteBlockIndex(blockindex))
            return error("Failed to write block index");
    }

    m_bestBlock = pindex->GetBlockHeader().GetHash();   
    return true;
}

bool TrieView::BalancesAt(CBlockIndex* pindex, vector<uint160> hashes, vector<CActInfo> &balances){
    LOCK(cs_main);
    uint256 oldHash = m_bestBlock;

    //Blast from the past
    uint256 bad;
    // Activation failure must not abort(); on failure we append default
    // CActInfo entries and let the RPC caller decide.
    if (!Activate(pindex, bad)) {
        for (size_t i = 0; i < hashes.size(); i++)
            balances.push_back(CActInfo());
        return false;
    }

    for(vector<uint160>::iterator it = hashes.begin(); it != hashes.end(); it++){
	TrieNode *node = TrieEngine::Find(*it, m_root);
	if(node){
	    CActInfo info(node);
	    uint64_t limit = node->Limit();
    	    if(limit != node->FutureLimit() && (pindex->nHeight - node->Age()) > MIN_LIMIT_TIME)
		info.limit = node->FutureLimit();
	    balances.push_back(info);
	}else{
	    balances.push_back(CActInfo());
	}
    }

    //Restore -- if the restore fails we still return whatever we have; the
    //tip will simply remain at the activated pindex until a future Activate
    // call fixes it. This avoids the previous abort() when the chain has
    // been pruned or rewound underneath us.
    Activate((*mapBlockIndex.find(oldHash)).second, bad);

    return true;
}

bool TrieView::Balance(uint160 key, uint64_t &balance){
    LOCK(cs_main);
    TrieNode *node = TrieEngine::Find(key, m_root);
    if(!node)
	return false;
    balance = node->Balance();
    return true;
}

bool TrieView::Limit(uint160 key, uint64_t &limit, uint64_t height){
    LOCK(cs_main);
    TrieNode *node = TrieEngine::Find(key, m_root);
    if(!node)
	return false;
    limit = node->Limit();
    if(limit != node->FutureLimit() && (height - node->Age()) > MIN_LIMIT_TIME)
	limit = node->FutureLimit();
    return true;
}


//Disregard any deposits in some region. Also include withdrawals from mempool
bool TrieView::ComplexBalances(int nMineConf, int nTheirsConf, vector<uint160> hashes, vector<CActInfo> &balances){
    LOCK(cs_main);

    // Programmer invariant: callers must ask for a "trusted" range that is
    // narrower than the "total" range. If violated, fall back to treating
    // the request as conservative (nMineConf == nTheirsConf) rather than
    // aborting.
    Assume(nMineConf <= nTheirsConf);

    //Get list of all tx information back to pindex
    vector<CBlock> blocks;
    CBlockIndex *phead = nullptr;
    if (!LookupBlockIndex(m_bestBlock, &phead)) {
        // No best-block means we have no chain to look at; return default
        // (zero) balances for every requested hash.
        for (size_t i = 0; i < hashes.size(); i++)
            balances.push_back(CActInfo());
        return false;
    }
    int runs = nTheirsConf-1;
    for(int i=0; i < runs; i++){
	CBlock block;
	if(!phead){
	    nMineConf--;
	    nTheirsConf--;
	    continue;
	}
        // A failed block read (pruned/missing) must not abort(); skip the
        // contribution from that block and continue. The resulting balance
        // will be slightly conservative -- which is the safe direction.
        if (!blockCache.ReadBlockFromDisk(block, phead))
            continue;
	blocks.push_back(block);
	phead = phead->pprev;
    }


    for(vector<uint160>::iterator it=hashes.begin(); it!= hashes.end(); it++){
	TrieNode *node = TrieEngine::Find(*it, m_root);
	if(node){
	    uint64_t balance = node->Balance();
	    uint64_t deps=0;
	    uint64_t withdrawals=0;
	    uint64_t age=node->Age();
	    uint64_t limit=node->Limit();
	    uint64_t futurelimit=node->FutureLimit();
	    uint64_t ofst = nTheirsConf;
    	    if(limit != node->FutureLimit() && (chainActive.Height() - node->Age()) > (MIN_LIMIT_TIME+ofst))
		limit = node->FutureLimit();

	    //We want to remove effect of any txouts in the untrusted region, this could be optimized 
	    for(int i=0; i < (int)blocks.size(); i++){
		CBlock block=blocks[i];
		for(vector<CTransaction>::iterator it3=block.vtx.begin(); it3!=block.vtx.end(); it3++){
		    CTransaction tx = *it3;
		    for(vector<CTxOut>::iterator it4=tx.vout.begin(); it4!=tx.vout.end(); it4++){
			CTxOut txout = *it4;
#ifdef ENABLE_WALLET
			if(txout.pubKey == *it && (i<(nMineConf-1) || !pwalletMain->IsFromMe(tx))){
#else
			if(txout.pubKey == *it){
#endif
			    deps+=txout.nValue;
			    if(block.nHeight > age)
				age = block.nHeight;
			}
		    }
		    		}
	    }

	    //Include any withdrawals in mempool
	    vector<CTransaction> vtx;
	    mempool.lookup(*it,vtx);
	    for(vector<CTransaction>::iterator it2=vtx.begin(); it2!=vtx.end(); it2++){
		//printf("Found tx\n");
		CTransaction tx = *it2;
		if(!IsFinalTx(tx))
		    continue;
		for(vector<CTxIn>::iterator it3=tx.vin.begin(); it3!=tx.vin.end(); it3++){
		    CTxIn txin=*it3;
		    //printf("%s,%s %ld\n", txin.pubKey.GetHex().c_str(), it->GetHex().c_str(), txin.nValue);
		    if(txin.pubKey == *it){
			withdrawals+=txin.nValue;
			age = chainActive.Height() + 1;
		    }
		}
		for(vector<CTxOut>::iterator it3=tx.vout.begin(); it3!=tx.vout.end(); it3++){
		    CTxOut txout=*it3;
		    if(txout.pubKey == *it){
#ifdef ENABLE_WALLET
			if((pwalletMain->IsFromMe(tx) && nMineConf==0) || nTheirsConf==0)
#else
			if(nTheirsConf==0)
#endif
			    balance+=txout.nValue;
		    }
		}
		if(tx.fSetLimit){
			   //TODO: there are cases where we can use an enlarged limit
			   //like if a queued update is old enough in chain it should switch
		#ifdef ENABLE_WALLET
			   bool fTrusted = (pwalletMain->IsFromMe(tx) && nMineConf==0) || nTheirsConf==0;
		#else
			   bool fTrusted = (false) || nTheirsConf==0;
		#endif
			   bool fSmaller = tx.nLimitValue < limit;
			   if(fTrusted || !fSmaller){
				futurelimit = tx.nLimitValue;
			   }
			}
	    }

	    if(deps + withdrawals > balance)
		balances.push_back(CActInfo());
	    else
	    	balances.push_back(CActInfo(balance-deps-withdrawals,age,limit,futurelimit));
	}else{
	    balances.push_back(CActInfo());
	}
    }

    return true;
}

uint64_t TrieView::CoinAge(uint160 pubkey){
    //Include any withdrawals in mempool
#if 0
    vector<CTransaction> vtx;
    mempool.lookup(pubkey,vtx);
    for(vector<CTransaction>::iterator it=vtx.begin(); it!=vtx.end(); it++){
	CTransaction tx = *it;
	if(!IsFinalTx(tx))
	    continue;
	for(vector<CTxIn>::iterator it2=tx.vin.begin(); it2!=tx.vin.end(); it2++){
	    CTxIn txin = *it2;
	    if(txin.pubKey == pubkey)
		return 0;
	}
	for(vector<CTxOut>::iterator it3=tx.vout.begin(); it3!=tx.vout.end(); it3++){
	    CTxOut txout = *it3;
	    if(txout.pubKey == pubkey)
		return 0;
	}
    }
#endif
    TrieNode* node = TrieEngine::Find(pubkey, m_root);
    // Account not present in the trie: safe default is "no priority boost",
    // which is what zero coin-age contributes to mempool priority.
    if (node == nullptr)
        return 0;
    return chainActive.Height()-node->Age()+1;
}

//Disregard any deposits in some region. Also include withdrawals from mempool
bool TrieView::ConservativeBalances(int nMinConf, vector<uint160> hashes, vector<CActInfo> &balances){
    return ComplexBalances(nMinConf,nMinConf,hashes,balances);
}

uint32_t TrieView::GetSlice(uint256 block, uint160 left, uint160 right, uint8_t *buf, uint32_t sz, uint32_t *nodes){
    LOCK(cs_main);
    uint256 oldHash = m_bestBlock;

    // Non-inserting lookups. mapBlockIndex[oldHash] silently inserts
    // (hash, nullptr) on miss and then dereferences it -- the original
    // assert fired on the empty-chain case before the first Activate.
    CBlockIndex* pOld = nullptr;
    CBlockIndex* pNew = nullptr;
    if (!LookupBlockIndex(oldHash, &pOld) || !LookupBlockIndex(block, &pNew))
        return 0;

    //Blast from the past
    uint256 bad;
    if(!Activate(pNew, bad)){
        return 0;
    }

    uint32_t pos=0;
    if(!TrieEngine::SubTrie(m_root,left,right,buf,&pos,(size_t)sz,nodes)){
        //Restore -- best-effort. On failure leave the trie wherever it
        // ended up; the next Activate() call will repair it.
        Activate(pOld, bad);
        return 0;
    }

#if 0
    TrieNode *trie = TrieNode::Deserialize(buf,sz);
    trie->Print();
    delete trie;
#endif

    //Restore -- best-effort, see above.
    Activate(pOld, bad);

    return pos;
}

bool TrieView::Flush(){
    LOCK(cs_main);
    // Nothing to flush on an empty/unbuilt trie. Returning false is the
    // safe default -- callers (Force, ActivateBestChainStep) already
    // tolerate a failed flush.
    if (m_root == nullptr) {
        LogPrintf("Flush: m_root null, skipping\n");
        return false;
    }
    LogPrintf("Writing file %s\n", m_bestBlock.GetHex().c_str());

    namespace fs = boost::filesystem;
    fs::path pathTrie = GetDataDir() / "trie.dat";
    fs::path pathTrieTmp = pathTrie;
    pathTrieTmp += TRIE_FILE_TMP_SUFFIX;

    // Serialize the trie into a heap buffer under cs_main so concurrent
    // m_root mutations cannot race with us. The on-disk budget is the
    // same heuristic the legacy code used: 200 bytes per leaf is an
    // overestimate that grows the buffer large enough for any realistic
    // SubTrie output; SubTrie() itself returns false if pos overflows.
    size_t fsize = m_root->Children() * 200; //TODO: define this as max size of trie node
    uint8_t* buf = (uint8_t*)malloc(fsize);
    if (!buf) {
        LogPrintf("WARNING: Flush: alloc(%zu) failed\n", fsize);
        return false;
    }
    uint32_t pos = 0;
    uint32_t nodes = 0;
    uint160_t left, right;
    left = 0;
    memset(&right, 0xFF, 20);
    if (!TrieEngine::SubTrie(m_root, left, right, buf, &pos, fsize, &nodes)) {
        LogPrintf("WARNING: Flush: SubTrie overflow (budget %zu, used %u)\n", fsize, pos);
        free(buf);
        return false;
    }
    LogPrintf("Serialized: %d bytes\n", pos);

    // Pack the on-disk record into one contiguous buffer. Layout:
    //   out[0..4]                       = magic           "XCNA"
    //   out[4..8]                       = version         TRIE_FILE_VERSION
    //   out[8..12]                      = payload_len     uint32, little-endian
    //   out[12..16]                     = CRC32C          masked, covers the body below
    //   out[16..48]                     = bestBlock       uint256
    //   out[48 .. 48+payload_len)       = payload         serialized trie
    // The CRC32C slot is filled last, after bestBlock and the payload
    // are in their final positions; covering both fields together
    // means an attacker (or a partial-write bug) cannot flip one
    // without invalidating the checksum on the other.
    const uint32_t payload_len = pos;
    const size_t   body_len     = TRIE_BESTBLOCK_LEN + payload_len;
    unsigned char* out = (unsigned char*)malloc(TRIE_FILE_HEADER_LEN + body_len);
    if (!out) {
        LogPrintf("WARNING: Flush: alloc(%zu) for output buffer failed\n",
                  TRIE_FILE_HEADER_LEN + body_len);
        free(buf);
        return false;
    }
    memcpy(out,                       TRIE_FILE_MAGIC, 4);
    uint32_t version = TRIE_FILE_VERSION;
    memcpy(out + 4,                   &version, 4);
    memcpy(out + 8,                   &payload_len, 4);
    // CRC slot is left zero here; filled after bestBlock + payload.
    memset(out + 12,                  0, 4);
    memcpy(out + TRIE_FILE_HEADER_LEN, &m_bestBlock, TRIE_BESTBLOCK_LEN);
    memcpy(out + TRIE_FILE_HEADER_LEN + TRIE_BESTBLOCK_LEN, buf, payload_len);
    free(buf);
    // CRC covers the body (bestBlock || payload), starting at the
    // beginning of the body so the read-side can validate the exact
    // same range.
    uint32_t crc = leveldb::crc32c::Value(
        reinterpret_cast<const char*>(out + TRIE_FILE_HEADER_LEN), body_len);
    crc = leveldb::crc32c::Mask(crc);
    memcpy(out + 12,                  &crc, 4);

    const size_t total = TRIE_FILE_HEADER_LEN + body_len;

    // Atomic write: tmp file -> fsync -> rename over the live file.
    // fopen("wb") truncates any existing tmp; the live trie.dat is
    // untouched until the rename succeeds.
    FILE* fileout = fopen(pathTrieTmp.string().c_str(), "wb");
    if (!fileout) {
        LogPrintf("WARNING: Flush: fopen(%s) failed (errno=%s)\n",
                  pathTrieTmp.string().c_str(), std::strerror(errno));
        free(out);
        return false;
    }
    // Disable stdio buffering so fwrite writes the exact byte count
    // via the underlying write() syscall and fflush+FileCommit covers
    // only the real data (the default 4 KiB stdio buffer would carry
    // uninitialised padding on a partial fill, which both inflates
    // the file's apparent size and risks leaking heap contents to
    // disk on a short write).
    setvbuf(fileout, nullptr, _IONBF, 0);

    bool ok = true;
    size_t written = fwrite(out, 1, total, fileout);
    if (written != total) {
        LogPrintf("WARNING: Flush: fwrite wrote %zu of %zu bytes\n", written, total);
        ok = false;
    } else {
        // Force stdio to flush user-space buffers, then ask the OS to
        // durably commit to disk. FileCommit is the platform-portable
        // helper from util.cpp (fdatasync on Linux/NetBSD,
        // F_FULLFSYNC on macOS, FlushFileBuffers on Windows).
        fflush(fileout);
        FileCommit(fileout);
    }
    fclose(fileout);

    if (!ok) {
        free(out);
        // Best-effort cleanup of the partial tmp file; the next Flush
        // will overwrite it whether or not this remove succeeds.
        try { fs::remove(pathTrieTmp); } catch (...) {}
        return false;
    }

    // Atomic-replace trie.dat with the freshly-synced tmp file. On
    // POSIX, rename(2) is atomic within a filesystem: the directory
    // entry either points at the old file or the new one, never at a
    // half-state. On Windows, boost::filesystem::rename uses
    // MoveFileEx(..., MOVEFILE_REPLACE_EXISTING) which provides the
    // same atomic-replace guarantee. If this fails (cross-FS link,
    // permission denied), the tmp file is left in place and the live
    // trie.dat is unchanged -- the daemon can retry on the next Flush.
    try {
        fs::rename(pathTrieTmp, pathTrie);
    } catch (const fs::filesystem_error& e) {
        LogPrintf("WARNING: Flush: atomic rename %s -> %s failed (%s); live trie.dat unchanged\n",
                  pathTrieTmp.string().c_str(), pathTrie.string().c_str(),
                  e.what());
        try { fs::remove(pathTrieTmp); } catch (...) {}
        free(out);
        return false;
    }

    free(out);
    return true;
}
