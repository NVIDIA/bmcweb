#include "utils/nvidia_utils.hpp"

#include <gtest/gtest.h>

namespace redfish
{
namespace
{
TEST(NvidiaUtils, join)
{
    EXPECT_EQ(join({"a", "b"}, ", "), "a, b");
    EXPECT_EQ(join({}, ", "), "");
    EXPECT_EQ(join({"1"}, "razzle"), "1");
}

TEST(NvidiaUtils, trim)
{
    EXPECT_EQ(trim(""), "");
    EXPECT_EQ(trim(" "), "");
    EXPECT_EQ(trim(" foo"), "foo");
    EXPECT_EQ(trim("foo "), "foo");
    EXPECT_EQ(trim(" foo "), "foo");
    EXPECT_EQ(trim("\tfoo\t"), "foo");
    EXPECT_EQ(trim("\nfoo\n"), "foo");
}
} // namespace
} // namespace redfish
