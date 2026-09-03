Cryptonite Core integration/staging tree
=====================================

http://cryptonite.info

Copyright (c) 2014 The Mini-blockchain Project<br />
Copyright (c) 2009-2026 Cryptonite Core Developers

What is Cryptonite
----------------

Cryptonite (also known as Crypton) is the first cryptocurrency to implement the mini-blockchain scheme along with many other innovative features. Cryptonite is a fork of the Bitcoin core but the code has been extensively modified and expanded upon. Innovative new features include withdrawal limits and unmalleable transactions. The mini-blockchain scheme alleviates the blockchain bloat problem and therefore allows superior support for micro-transactions and arbitrary data to be stored in the blockchain.

For more information see http://cryptonite.info

License
-------

Cryptonite Core is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see http://opensource.org/licenses/MIT.

Development process
-------------------

Developers work in their own trees, then submit pull requests when they think
their feature or bug fix is ready.

If it is a simple/trivial/non-controversial change, then one of the Cryptonite
development team members simply pulls it.

If it is a *more complicated or potentially controversial* change, then the patch
submitter will be asked to start a discussion (if they haven't already).

The patch will be accepted if there is broad consensus that it is a good thing.
Developers should expect to rework and resubmit patches if the code doesn't
match the project's coding conventions (see [doc/coding.md](doc/coding.md)) or are
controversial.

Testing
-------

### Automated Testing

Developers are strongly encouraged to write unit tests for new code, and to
submit new unit tests for old code. Unit tests can be compiled and run (assuming they weren't disabled in configure) with: `make check`

Every pull request is built for both Windows and Linux on a dedicated server,
and unit and sanity tests are automatically run.

### Manual Quality Assurance (QA) Testing

Large changes should have a test plan, and should be tested by somebody other
than the developer who wrote the code.

Building on Linux (Ubuntu 26.04+)
---------------------------------

Canonical configuration:

    ./autogen.sh
    ./configure --enable-wallet --with-incompatible-bdb --without-gui \
                --without-miniupnpc --disable-hardening --enable-tests
    make
    make check     # runs test_cryptonite (59 cases)
    make distcheck # builds the dist tarball end-to-end

Optional flags that have been verified to work:

    --disable-wallet       # builds without the wallet library
    --with-miniupnpc       # enables UPnP port mapping (libminiupnpc-dev)
    --enable-hardening     # adds PIE, Full RELRO, -D_FORTIFY_SOURCE=2, stack protector

Cryptonite's COIN constant is 10^10 (10-digit "ep" precision). Sub-cent
amounts such as 0.0000001 XCN are valid; the canonical config builds and
the daemon prints these at full precision.

Notes:

* The Qt GUI is not currently supported. `src/qt/` is excluded from the
  build and from `make distcheck` because it targets Qt4 APIs that are
  not available in Ubuntu 26.04.
* BDB 4.8 is no longer in apt repos. Use BDB 5.3 (`libdb5.3++-dev`) and
  pass `--with-incompatible-bdb` to configure.
* `make distcheck` is supported and produces `cryptonite-0.2.0.tar.gz`.
