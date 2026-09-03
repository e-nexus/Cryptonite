Cryptonite Core 0.1.7
======================

Copyright (c) 2009-2014 Bitcoin Developers
Copyright (c) 2014 The Mini-Blockchain Project

Setup
---------------------
[Bitcoin Core](http://bitcoin.org/en/download) is the original Bitcoin
client and it builds the backbone of the network. However, it downloads
and stores the entire history of Bitcoin transactions (which is
currently several GBs); depending on the speed of your computer and
network connection, the synchronization process can take anywhere from
a few hours to a day or more. Cryptonite is a fork of Bitcoin Core that
ships with its own chainstate and consensus rules; you only need to do
the initial sync once. To speed up the process you can
[bootstrap the chainstate](bootstrap.md).

Running
---------------------
The following are some helpful notes on how to run Cryptonite on your
native platform.

Note: the Qt GUI is not currently supported in this fork. Cryptonite
ships only the headless daemon (`cryptonited`) and the RPC client
(`cryptonite-cli`).

### Unix

Unpack the files into a directory and run:

- bin/64/cryptonited   (headless daemon, 64-bit)
- bin/64/cryptonite-cli (RPC client, 64-bit)

You will need a working Berkeley DB 5.3 (libdb5.3++-dev) and Boost
1.70 or newer to build from source; see [build-unix.md](build-unix.md).

### Windows

The Windows build is not currently exercised; see [build-msw.md](build-msw.md)
for the build procedure. Cryptonite produces `cryptonited.exe` and
`cryptonite-cli.exe`.

### OSX

The macOS build is not currently exercised; see [build-osx.md](build-osx.md).
Cryptonite produces `./cryptonited` and `./cryptonite-cli` from the
src/ directory after `./configure && make`.

### Need Help?

* See [build-unix.md](build-unix.md) for build prerequisites and flags.
* Project home page: http://cryptonite.info

Building
---------------------
The following are developer notes on how to build Bitcoin on your native platform. They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

- [OSX Build Notes](build-osx.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-msw.md)

Development
---------------------
The Bitcoin repo's [root README](https://github.com/bitcoin/bitcoin/blob/master/README.md) contains relevant information on the development process and automated testing.

- [Coding Guidelines](coding.md)
- [Multiwallet Qt Development](multiwallet-qt.md)
- [Release Notes](release-notes.md)
- [Release Process](release-process.md)
- [Source Code Documentation (External Link)](https://dev.visucore.com/bitcoin/doxygen/)
- [Translation Process](translation_process.md)
- [Unit Tests](unit-tests.md)

### Resources
* Discuss on the [BitcoinTalk](https://bitcointalk.org/) forums, in the [Development & Technical Discussion board](https://bitcointalk.org/index.php?board=6.0).
* Discuss on [#bitcoin-dev](http://webchat.freenode.net/?channels=bitcoin) on Freenode. If you don't have an IRC client use [webchat here](http://webchat.freenode.net/?channels=bitcoin-dev).

### Miscellaneous
- [Assets Attribution](assets-attribution.md)
- [Files](files.md)
- [Tor Support](tor.md)

License
---------------------
Distributed under the [MIT/X11 software license](http://www.opensource.org/licenses/mit-license.php).
This product includes software developed by the OpenSSL Project for use in the [OpenSSL Toolkit](http://www.openssl.org/). This product includes
cryptographic software written by Eric Young ([eay@cryptsoft.com](mailto:eay@cryptsoft.com)), and UPnP software written by Thomas Bernard.
