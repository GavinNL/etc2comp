
#include <gtest/gtest.h>

#include <EtcBlock4x4.h>

TEST(Block4x4Test, Construction) {
  Etc::Block4x4 block;
  auto const alphaMix = block.GetSourceAlphaMix();
  ASSERT_EQ(alphaMix, Etc::Block4x4::SourceAlphaMix::UNKNOWN);
  ASSERT_FALSE(block.HasBorderPixels());
  ASSERT_FALSE(block.HasPunchThroughPixels());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
