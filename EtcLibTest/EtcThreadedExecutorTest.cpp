
#include <random>

#include <gtest/gtest.h>

#include "EtcThreadedExecutor.h"

namespace {

constexpr unsigned int uiSourceWidth = 1024;
constexpr unsigned int uiSourceHeight = 1024;
constexpr std::mt19937::result_type SEED = 1982;

class ThreadedExecutorTest : public ::testing::Test {
protected:
  void SetUp() override;

  std::array<float, uiSourceWidth * uiSourceHeight * 4> imageData_;
  std::optional<Etc::Image> image_;
};

void
ThreadedExecutorTest::SetUp() {
  static_assert(sizeof(Etc::ColorFloatRGBA) == 4 * sizeof(float));

  std::mt19937 gen(SEED);
 
  std::uniform_real_distribution<float> dis;

  std::generate(imageData_.begin(), imageData_.end(), [&]() {
    return dis(gen);
  });

  image_.emplace(imageData_.data(),
    uiSourceWidth, uiSourceHeight,
    Etc::ErrorMetric::RGBA);
}

} // namespace

TEST_F(ThreadedExecutorTest, EncodeWithInvalidJobs) {
  Etc::ThreadedExecutor executor(*image_);
  ASSERT_EQ(executor.Encode(Etc::Image::Format::RGBA8, Etc::ErrorMetric::RGBA, 100, -1, 0), Etc::Executor::EncodingStatus::SUCCESS);
}

TEST_F(ThreadedExecutorTest, EncodeWithTooManyJobs) {
  Etc::ThreadedExecutor executor(*image_);
  ASSERT_EQ(executor.Encode(Etc::Image::Format::ETC1, Etc::ErrorMetric::RGBA, 90, 5, 2), Etc::Executor::EncodingStatus::SUCCESS);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
