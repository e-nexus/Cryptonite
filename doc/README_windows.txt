Cryptonite Core 0.1.7

Copyright (c) 2009-2014 Bitcoin Core Developers
Copyright (c) 2014 The Mini-Blockchain Project

Distributed under the MIT/X11 software license, see the accompanying
file COPYING or http://www.opensource.org/licenses/mit-license.php.
This product includes software developed by the OpenSSL Project for use in
the OpenSSL Toolkit (http://www.openssl.org/).  This product includes
cryptographic software written by Eric Young (eay@cryptsoft.com).


Intro
-----
Cryptonite is a free open source peer-to-peer electronic cash system that is
completely decentralized, without the need for a central server or trusted
parties.  Users hold the crypto keys to their own money and transact directly
with each other, with the help of a P2P network to check for double-spending.

Note: the Qt GUI is not currently supported in this fork. Cryptonite ships
only the headless daemon (cryptonited.exe) and the RPC client
(cryptonite-cli.exe).


Setup
-----
Unpack the files into a directory and run cryptonited.exe.

Cryptonite Core is a fork of Bitcoin Core that ships with its own chainstate
and consensus rules. It downloads and stores the entire history of
transactions; depending on the speed of your computer and network
connection, the synchronization process can take anywhere from a few hours
to a day or more.

See the project home page at:
  http://cryptonite.info
for more help and information.