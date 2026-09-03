// Copyright (c) 2014 The Mini-Blockchain Project
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Public re-export of the vendored LevelDB CRC32C implementation so that
// callers outside the leveldb tree can use the same CRC32C primitive that
// LevelDB itself uses for its log records and SST blocks. The underlying
// implementation lives in src/leveldb/util/crc32c.{h,cc} and is compiled
// into libleveldb.a; this header is a one-line forwarding shim so the
// stable public include path (<leveldb/crc32c.h>) stays usable.

#ifndef STORAGE_LEVELDB_INCLUDE_CRC32C_H_
#define STORAGE_LEVELDB_INCLUDE_CRC32C_H_

#include "../../util/crc32c.h"

#endif  // STORAGE_LEVELDB_INCLUDE_CRC32C_H_
