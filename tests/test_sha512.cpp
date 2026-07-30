#include "core/crypto/Sha512.hpp"

#include <string>

#include "microtest.hpp"

using namespace uaro;

// FIPS 180-4 / NIST known-answer vectors.
TEST_CASE(sha512_empty) {
    CHECK(Sha512::hashHex("", 0) ==
          "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
          "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");
}

TEST_CASE(sha512_abc) {
    CHECK(Sha512::hashHex("abc", 3) ==
          "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
          "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
}

TEST_CASE(sha512_two_block) {
    // 112-char message -> exercises the multi-block padding path.
    const char* m = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                    "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    CHECK(Sha512::hashHex(m, 112) ==
          "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
          "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909");
}

TEST_CASE(sha512_streaming_matches_oneshot) {
    const std::string msg = "The quick brown fox jumps over the lazy dog";
    Sha512 h;
    // Feed in awkward chunk sizes to exercise the buffering.
    h.update(msg.data(), 5);
    h.update(msg.data() + 5, 1);
    h.update(msg.data() + 6, msg.size() - 6);
    CHECK(Sha512::toHex(h.finalize()) == Sha512::hashHex(msg.data(), msg.size()));
    // And the known digest for this classic message.
    CHECK(Sha512::hashHex(msg.data(), msg.size()) ==
          "07e547d9586f6a73f73fbac0435ed76951218fb7d0c8d788a309d785436bbb64"
          "2e93a252a954f23912547d1e8a3b5ed6e1bfd7097821233fa0538f3db854fee6");
}
