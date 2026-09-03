// Copyright (c) 2012-2013 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpcserver.h"
#include "rpcclient.h"

#include "base58.h"
#include "core.h"
#include "serialize.h"
#include "uint256.h"

#include <boost/algorithm/string.hpp>
#include <boost/test/unit_test.hpp>

using namespace std;
using namespace json_spirit;

Array
createArgs(int nRequired, const char* address1=nullptr, const char* address2=nullptr)
{
    Array result;
    result.push_back(nRequired);
    Array addresses;
    if (address1) addresses.push_back(address1);
    if (address2) addresses.push_back(address2);
    result.push_back(addresses);
    return result;
}

Value CallRPC(string args)
{
    vector<string> vArgs;
    boost::split(vArgs, args, boost::is_any_of(" \t"));
    string strMethod = vArgs[0];
    vArgs.erase(vArgs.begin());
    Array params = RPCConvertValues(strMethod, vArgs);

    rpcfn_type method = tableRPC[strMethod]->actor;
    try {
        Value result = (*method)(params, false);
        return result;
    }
    catch (Object& objError)
    {
        throw runtime_error(find_value(objError, "message").get_str());
    }
}


BOOST_AUTO_TEST_SUITE(rpc_tests)

BOOST_AUTO_TEST_CASE(rpc_rawparams)
{
    // Test raw transaction API argument handling
    Value r;

    BOOST_CHECK_THROW(CallRPC("getrawtransaction"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("getrawtransaction not_hex"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("getrawtransaction a3b807410df0b60fcb9736768df5823938b2f838694939ba45f3c0a1bff150ed not_int"), runtime_error);

    BOOST_CHECK_THROW(CallRPC("createrawtransaction"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("createrawtransaction null null"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("createrawtransaction not_array"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("createrawtransaction [] []"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("createrawtransaction [] {}"), runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("createrawtransaction {} {}"));
    BOOST_CHECK_THROW(CallRPC("createrawtransaction {} {} extra"), runtime_error);

    BOOST_CHECK_THROW(CallRPC("decoderawtransaction"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("decoderawtransaction null"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("decoderawtransaction DEADBEEF"), runtime_error);
    // Build a Cryptonite-shape transaction and feed its hex to the RPC.
    // The original Bitcoin Core test used a Bitcoin-Core format tx which
    // cannot be deserialized under Cryptonite's uint160-pubKey I/O model.
    uint160 K1; K1.SetHex("1111111111111111111111111111111111111111");
    uint160 K3; K3.SetHex("3333333333333333333333333333333333333333");
    CTransaction rawtxObj;
    rawtxObj.SetNull();
    rawtxObj.nVersion = 1;
    rawtxObj.vin.push_back(CTxIn(K1, 100 * COIN));
    rawtxObj.vout.push_back(CTxOut(99 * COIN, K3));
    CDataStream txss(SER_NETWORK, PROTOCOL_VERSION);
    txss << rawtxObj;
    string rawtxHex = HexStr(txss.begin(), txss.end());
    BOOST_CHECK_NO_THROW(r = CallRPC(string("decoderawtransaction ") + rawtxHex));
    BOOST_CHECK_EQUAL(find_value(r.get_obj(), "version").get_int(), 1);
    BOOST_CHECK_EQUAL(find_value(r.get_obj(), "lockheight").get_int(), 0);
    BOOST_CHECK_THROW(r = CallRPC(string("decoderawtransaction ") + rawtxHex + " extra"), runtime_error);

    BOOST_CHECK_THROW(CallRPC("signrawtransaction"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("signrawtransaction null"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("signrawtransaction ff00"), runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC(string("signrawtransaction ") + rawtxHex));
    BOOST_CHECK_NO_THROW(CallRPC(string("signrawtransaction ") + rawtxHex + " null"));
    // Cryptonite's RPCTypeCheck rejects an empty array for params[1] because the
    // expected type for that slot is an object (map of input index -> required sigs);
    // pass "null" instead when no prev-tx info is available.
    BOOST_CHECK_THROW(CallRPC(string("signrawtransaction ") + rawtxHex + " []"), runtime_error);

    // Only check failure cases for sendrawtransaction, there's no network to send to...
    BOOST_CHECK_THROW(CallRPC("sendrawtransaction"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("sendrawtransaction null"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("sendrawtransaction DEADBEEF"), runtime_error);
    BOOST_CHECK_THROW(CallRPC(string("sendrawtransaction ") + rawtxHex + " extra"), runtime_error);
}

#if 0
BOOST_AUTO_TEST_CASE(rpc_rawsign)
{

    Value r;
    // input is a 1-of-2 multisig (so is output):
    string prevout =
      "[{\"txid\":\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\","
      "\"vout\":1,\"scriptPubKey\":\"a914b10c9df5f7edf436c697f02f1efdba4cf399615187\","
      "\"redeemScript\":\"512103debedc17b3df2badbcdd86d5feb4562b86fe182e5998abd8bcd4f122c6155b1b21027e940bb73ab8732bfdf7f9216ecefca5b94d6df834e77e108f68e66f126044c052ae\"}]";
    r = CallRPC(string("createrawtransaction ")+prevout+" "+
      "{\"3HqAe9LtNBjnsfM4CyYaWTnvCaUYT7v4oZ\":11}");
    string notsigned = r.get_str();
    string privkey1 = "\"KzsXybp9jX64P5ekX1KUxRQ79Jht9uzW7LorgwE65i5rWACL6LQe\"";
    string privkey2 = "\"Kyhdf5LuKTRx4ge69ybABsiUAWjVRK4XGxAKk2FQLp2HjGMy87Z4\"";
    r = CallRPC(string("signrawtransaction ")+notsigned+" "+prevout+" "+"[]");
    BOOST_CHECK(find_value(r.get_obj(), "complete").get_bool() == false);
    r = CallRPC(string("signrawtransaction ")+notsigned+" "+prevout+" "+"["+privkey1+","+privkey2+"]");
    BOOST_CHECK(find_value(r.get_obj(), "complete").get_bool() == true);
}
#endif


BOOST_AUTO_TEST_CASE(rpc_format_monetary_values)
{
    // Cryptonite uses 10-digit precision (COIN = 10^10) for "ep" (extended-precision) amounts.
    BOOST_CHECK(ValueFromAmount(0LL).get_str() == "0.0000000000ep");
    BOOST_CHECK(ValueFromAmount(1LL).get_str() == "0.0000000001ep");
    BOOST_CHECK(ValueFromAmount(17622195LL).get_str() == "0.0017622195ep");
    BOOST_CHECK(ValueFromAmount(50000000LL).get_str() == "0.0050000000ep");
    BOOST_CHECK(ValueFromAmount(89898989LL).get_str() == "0.0089898989ep");
    BOOST_CHECK(ValueFromAmount(100000000LL).get_str() == "0.0100000000ep");
    BOOST_CHECK(ValueFromAmount(2099999999999990LL).get_str() == "209999.9999999990ep");
    BOOST_CHECK(ValueFromAmount(2099999999999999LL).get_str() == "209999.9999999999ep");
}

static Value ValueFromString(const std::string &str)
{
    Value value;
    BOOST_CHECK(read_string(str, value));
    return value;
}

BOOST_AUTO_TEST_CASE(rpc_parse_monetary_values)
{
    // Cryptonite uses 10-digit precision (COIN = 10^10).
    BOOST_CHECK(AmountFromValue(ValueFromString("\"0.0000000001ep\"")) == 1LL);
    BOOST_CHECK(AmountFromValue(ValueFromString("\"0.0017622195ep\"")) == 17622195LL);
    BOOST_CHECK(AmountFromValue(ValueFromString("\"0.0050000000ep\"")) == 50000000LL);
    BOOST_CHECK(AmountFromValue(ValueFromString("\"0.0089898989ep\"")) == 89898989LL);
    BOOST_CHECK(AmountFromValue(ValueFromString("\"0.0100000000ep\"")) == 100000000LL);
    BOOST_CHECK(AmountFromValue(ValueFromString("\"209999.9999999999ep\"")) == 2099999999999999LL);
}

// RPCTypeCheck used to silently pass when the caller supplied zero params to an
// RPC expecting at least one, which let downstream code dereference params[0]
// and crash. Confirm empty-params calls now throw at the validation layer.
// Note: undersized-but-non-empty calls are still allowed because RPCs that take
// optional trailing arguments (signrawtransaction, createrawtransaction) rely
// on RPCTypeCheck accepting partial parameter lists and letting downstream
// code apply defaults.
BOOST_AUTO_TEST_CASE(rpc_empty_params_throws)
{
    BOOST_CHECK_THROW(CallRPC("getrawtransaction"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("decoderawtransaction"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("sendrawtransaction"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("createrawtransaction"), runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
