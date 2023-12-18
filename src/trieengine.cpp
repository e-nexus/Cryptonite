// Copyright (c) 2014 The Mini-Blockchain Project
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <iostream>
#include <vector>
#include <algorithm>
#include <list>

#include "uint256.h"
#include "hash.h"
#include "trie.h"

using namespace std;

static uint160_t sub_key(uint160_t key, uint32_t left, uint32_t size){
	return ((key << left) >> (160 - size)) << (160 - size - left);
}

static uint32_t high_bit(uint160_t key, uint32_t shift){
	key = key >> (160 - shift - 1);
	return *key.begin() & 1;
}

//Function to insert into trie
void TrieEngine::Insert(TrieNode **root, TrieNode *node, uint32_t bits) {
    if (*root == nullptr) {
        *root = node;
        node->SetParent(nullptr);
        return;
    }

    if ((*root)->Type() == NODE_BRANCH) {
        uint32_t new_bits = (*root)->Bits();
        uint160_t subkey = sub_key(node->Key(), bits, new_bits);
        if (subkey == (*root)->Key()) {
            uint32_t bit = high_bit(node->Key(), bits + new_bits);
            if (bit & 1) {
                Insert(&(*root)->m_right, node, bits + new_bits + 1);
            } else {
                Insert(&(*root)->m_left, node, bits + new_bits + 1);
            }
            return;
        }
    }

    if (true) { // Simplified conditional
        TrieNode *parent = (*root)->Parent();
        TrieNode *node2 = *root;
        uint160_t k1 = node->Key() << bits;
        uint160_t k2 = node2->Key() << bits;

        assert(k1 != k2);

        if (parent) {
            parent->Subtract((*root)->Children());
        }

        *root = new TrieNode(NODE_BRANCH);
        (*root)->SetParent(parent);
        
        if (k1 < k2) {
            (*root)->Add(node);
            (*root)->Add(node2);
        } else {
            (*root)->Add(node2);
            (*root)->Add(node);
        }

        uint8_t c1, c2;
        uint32_t new_bits = 0;
        do {
            c1 = *(k1.end() - 1);
            c2 = *(k2.end() - 1);

            k1 <<= 1;
            k2 <<= 1;
            new_bits++;
        } while (((c1 ^ c2) & 0x80) == 0);
        new_bits--;

        (*root)->SetBits(new_bits);
        (*root)->SetKey(sub_key(node->Key(), bits, new_bits));

        if ((node->Type() == NODE_BRANCH) || (node2->Type() == NODE_BRANCH)) {
            TrieNode *branch = (node2->Type() == NODE_BRANCH) ? node2 : node;
            branch->SetBits(branch->Bits() - new_bits - 1);
            branch->SetKey(sub_key(branch->Key(), bits + new_bits + 1, branch->Bits()));
        }
    }
}

void TrieEngine::Remove(TrieNode **root, TrieNode *node) {
    TrieNode *parent = node->Parent();
    if (!parent) {
        *root = nullptr;
        delete node;
        return;
    }

    TrieNode *peer = parent->Remove(node);
    delete node;

    if (!parent->Parent()) {
        *root = peer;
        peer->SetParent(nullptr);
    } else {
        parent->Parent()->Replace(parent, peer);
    }

    if (peer->Type() == NODE_BRANCH) {
        peer->SetBits(peer->Bits() + parent->Bits() + 1);
        uint32_t total = parent->GetTotalBits();
        peer->SetKey(peer->Key() | parent->Key());
        if (parent->m_right == peer) {
            uint160_t foo = 1;
            foo <<= (160 - total);
            peer->SetKey(peer->Key() | foo);
        }
    }

    parent->SetParent(nullptr);
    parent->Remove(peer);
    delete parent;
}

uint64_t TrieEngine::Size(TrieNode* root){
	if(!root)
		return 0;
	return root->Children();
}

void SerializeHash(uint8_t *dst, uint32_t *pos, uint256_t hash){
	memcpy(dst+*pos,&hash,32);
	*pos+=32;
}

void DeserializeHash(uint256_t *hash,uint8_t *src){
	memcpy(hash,src,32);
}

void SerializeHash(uint8_t *dst, uint32_t *pos, uint160_t hash){
	memcpy(dst+*pos,&hash,20);
	*pos+=20;
}

void DeserializeHash(uint160_t *hash,uint8_t *src){
	memcpy(hash,src,20);
}

bool hashNode(TrieNode* root,uint8_t *dst, uint32_t *pos, uint32_t max){
	if(max - *pos < 33)
		return false;
	dst[(*pos)++] = NODE_HASH;
	uint256_t hash = root->Hash();
	SerializeHash(dst,pos,hash);
	return true;
}

bool TrieEngine::SubTrie(TrieNode* root, uint160_t left, uint160_t right, uint8_t *dst, uint32_t *pos, uint32_t max, uint32_t *nodes, uint160_t ckey, uint32_t bits, bool hashOnly){
	uint160_t ones;
	memset(&ones,0xFF,20);

	if(hashOnly)
		return hashNode(root,dst,pos,max);
	if(root->Type() == NODE_LEAF){
		(*nodes)++;
		return root->Serialize(dst,pos,max);
	}else{
		bits+=root->Bits();
		ckey |= root->Key();
		uint160_t maxkey = ckey | (((uint160_t)1) << (160-bits-1));
		uint160_t mask = ~(ones >> (bits+1));

//		cout << maxkey.GetHex() << ", " << ckey.GetHex() << ", " << left.GetHex() << ", " << mask.GetHex().c_str() << endl;
		bool hashLeft=false;
		bool hashRight=false;

		if(ckey < (left & mask))
		    hashLeft = true;

		if(maxkey < (left & mask))
		    hashRight = true;

		if(maxkey > right){
//			hashLeft=true;
			hashRight=true;
		}


		if(!root->Serialize(dst,pos,max))
			return false;

		if(!SubTrie(root->m_left,left,right,dst,pos,max,nodes,ckey,bits+1,hashLeft))
			return false;
		if(!SubTrie(root->m_right,left,right,dst,pos,max,nodes,maxkey,bits+1,hashRight))
			return false;
	}
	return true;
}

void TrieEngine::RebuildStructure(TrieNode *root){
	root->Parentify(0);
	root->FindChildren();
}

void TrieEngine::TraverseLeft(TrieNode *leftnode, uint160_t left, uint160_t right, list<TrieNode*> *lefts, int bits) {
    uint160_t ones;
    memset(&ones, 0xFF, 20);
    if (leftnode && leftnode->Type() == NODE_BRANCH) {
        bits += leftnode->Bits();
        uint160_t mask = ~(ones >> (bits + 1));
        uint160_t tkey = leftnode->GetTotalKey(leftnode->m_left, 0);
        uint160_t rtkey = leftnode->GetTotalKey(leftnode->m_right, 0);

        if (tkey < (left & mask)) {
            leftnode->FindAll(NODE_HASH, lefts);
        } else {
            TraverseLeft(leftnode->m_left, left, right, lefts, bits);
        }
        if (rtkey < (left & mask)) {
            leftnode->m_right->FindAll(NODE_HASH, lefts);
        } else {
            TraverseLeft(leftnode->m_right, left, right, lefts, bits);
        }
    }
}

void TrieEngine::TraverseRight(TrieNode *rightnode, uint160_t left, uint160_t right, list<TrieNode*> *rights, int bits) {
    uint160_t ones;
    memset(&ones, 0xFF, 20);
    if (rightnode && rightnode->Type() == NODE_BRANCH) {
        bits += rightnode->Bits();
        uint160_t rtkey = rightnode->GetTotalKey(rightnode->m_right, 0);

        if (rtkey > right) {
            rightnode->FindAll(NODE_HASH, rights);
        } else {
            TraverseRight(rightnode->m_right, left, right, rights, bits);
        }
        if (rtkey > left) {
            rightnode->m_left->FindAll(NODE_HASH, rights);
        } else {
            TraverseRight(rightnode->m_left, left, right, rights, bits);
        }
    }
}

bool TrieEngine::Prove(TrieNode *root, uint160_t left, uint160_t right) {
    uint160_t ones;
    memset(&ones, 0xFF, 20);

    if (!root) {
        printf("Empty trie provided.\n");
        return false;
    }

    // Locate all hash nodes to the left of left
    list<TrieNode*> lefts;
    TraverseLeft(root, left, right, &lefts, 0);

    // Do right traversal
    list<TrieNode*> rights;
    TraverseRight(root, left, right, &rights, 0);

    // For sanity check, find all hash nodes
    list<TrieNode*> hashnodes;
    root->FindAll(NODE_HASH, &hashnodes);

    // Remove duplicates from lefts and rights
    lefts.sort();
    lefts.unique();
    rights.sort();
    rights.unique();

    // Remove nodes in lefts and rights from hashnodes
    for (TrieNode* node : lefts) {
        hashnodes.remove(node);
    }
    for (TrieNode* node : rights) {
        hashnodes.remove(node);
    }

    // Check for remaining hash nodes
    if (!hashnodes.empty()) {
        printf("Bad trie! Unaccounted hash nodes found.\n");
        return false;
    }

    return true;
}

TrieNode* TrieEngine::Find(uint160_t key, TrieNode *root, uint32_t keybits) {
    if (!root) {
        return nullptr;
    }

    if (root->Type() == NODE_LEAF) {
        return (root->Key() == key) ? root : nullptr;
    }

    if (root->Type() == NODE_BRANCH) {
        uint160_t skey = sub_key(key, keybits, root->Bits());
        uint160_t key2 = sub_key(root->Key(), keybits, root->Bits());

        if (skey != key2) {
            return nullptr;
        }

        bool isRight = ((key >> (159 - (keybits + root->Bits()))) & 1) != 0;
        return Find(key, isRight ? root->m_right : root->m_left, keybits + root->Bits() + 1);
    }

    return nullptr;
}
