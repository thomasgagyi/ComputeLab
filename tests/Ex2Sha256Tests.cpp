#include "ex2/Ex2Sha256.hpp"

#include <gtest/gtest.h>

#include <string>

namespace
{

TEST(Ex2Sha256, MatchesIndependentStandardKnownAnswerVectors)
{
    EXPECT_EQ(computelab::ex2::Sha256(""),
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(computelab::ex2::Sha256("abc"),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(computelab::ex2::Sha256("abd"),
        "a52d159f262b2c6ddb724a61840befc36eb30c88877a4030b65cbe86298449c9");
}

TEST(Ex2Sha256, MatchesIndependentKnownAnswerBeyondOneCompressionBlock)
{
    const std::string millionAs(1'000'000U, 'a');
    EXPECT_EQ(computelab::ex2::Sha256(millionAs),
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

} // namespace
