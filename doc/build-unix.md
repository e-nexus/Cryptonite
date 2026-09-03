UNIX BUILD NOTES
================
Some notes on how to build Cryptonite in Unix. The Qt GUI is not
currently supported in this fork; only the headless daemon
(cryptonited) and the RPC client (cryptonite-cli) are produced.

To Build
--------

    ./autogen.sh
    ./configure --enable-wallet --with-incompatible-bdb --without-gui \
                --without-miniupnpc --disable-hardening --enable-tests
    make

Use `make check` afterwards to run the test suite.

Dependencies
------------

 Library     | Purpose          | Description
 ------------|------------------|----------------------
 libssl      | SSL Support      | Secure communications
 libdb5.3    | Berkeley DB      | Wallet storage (with --with-incompatible-bdb)
 libboost    | Boost            | C++ Library
 libgmp      | Large numbers    | GNU Multiprecision
 miniupnpc   | UPnP Support     | Optional firewall-jumping support (--with-miniupnpc)

Qt, protobuf and libqrencode are NOT used in this build because the Qt
GUI is disabled.

[miniupnpc](http://miniupnp.free.fr/) may be used for UPnP port mapping.
On Debian / Ubuntu it ships as `libminiupnpc-dev`. UPnP support is
compiled in and turned off by default. Configure flags:

    --without-miniupnpc      No UPnP support
    --with-miniupnpc         Build with UPnP support (default: off)

Licenses of statically linked libraries:
 Berkeley DB   New BSD license with additional requirement that linked
               software must be free open source
 Boost         MIT-like license
 miniupnpc     New (3-clause) BSD license


System requirements
-------------------

C++ compilers are memory-hungry. It is recommended to have at least 1 GB
of memory available when compiling Cryptonite. With 512MB of memory or
less compilation will take much longer due to swap thrashing.

Dependency Build Instructions: Ubuntu & Debian
----------------------------------------------

Build requirements (Ubuntu 26.04 / Debian 12 or newer):

    sudo apt-get install build-essential
    sudo apt-get install libtool autotools-dev autoconf
    sudo apt-get install libssl-dev libgmp-dev
    sudo apt-get install libboost-all-dev
    sudo apt-get install libdb5.3++-dev

For Berkeley DB: BDB 4.8 is no longer in apt repos. Use the system BDB
5.3 (libdb5.3++-dev) and pass `--with-incompatible-bdb` to configure.
This is the default in the canonical invocation shown above.

Optional:

    sudo apt-get install libminiupnpc-dev   # for --with-miniupnpc

GUI
---

The Qt GUI is not supported in this fork. src/qt/ targets Qt4 APIs that
are not available in Ubuntu 26.04 and is excluded from the build and
from `make distcheck`.


Notes
-----
The release is built with GCC and then "strip cryptonited" to strip the
debug symbols, which reduces the executable size by about 90%.


Security
--------
To help make your Cryptonite installation more secure by making
certain attacks impossible to exploit even if a vulnerability is found,
binaries can be hardened via:

    ./configure --enable-hardening

This adds Position Independent Executable (PIE), Full RELRO
(-Wl,-z,relro -Wl,-z,now), -D_FORTIFY_SOURCE=2 and stack-protector
flags. Disable with:

    ./configure --disable-hardening

The canonical config in this repository uses --disable-hardening so
the binaries can be debugged with line-level symbols; --enable-hardening
is fully supported and produces a verified PIE + Full RELRO binary.


Disable-wallet mode
-------------------
When the intention is to run only a P2P node without a wallet,
Cryptonite may be compiled in disable-wallet mode with:

    ./configure --disable-wallet

In this case there is no dependency on Berkeley DB and the binary is
substantially smaller (cryptonited drops from ~100 MB to ~79 MB
because libcryptonite_wallet.a is not linked).

Mining is also possible in disable-wallet mode, but only using the
`getblocktemplate` RPC call (not `getwork`).