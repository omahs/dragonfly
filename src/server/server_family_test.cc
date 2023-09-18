// Copyright 2022, DragonflyDB authors.  All rights reserved.
// See LICENSE for licensing terms.
//

#include <absl/strings/match.h>

#include "base/gtest.h"
#include "base/logging.h"
#include "server/test_utils.h"

namespace dfly {

class ServerFamilyTest : public BaseFamilyTest {
 protected:
};

TEST_F(ServerFamilyTest, ClientPause) {
  auto start = absl::Now();

  auto fb0 = pp_->at(0)->LaunchFiber(Launch::dispatch, [&] {
    Run("pause", {"CLIENT", "PAUSE", "50"});
  });

  Run({"get", "key"});
  EXPECT_GT((absl::Now() - start), absl::Milliseconds(50));
  fb0.Join();

  start = absl::Now();
  auto fb1 = pp_->at(0)->LaunchFiber(Launch::dispatch, [&] {
    Run("pause", {"CLIENT", "PAUSE", "50", "WRITE"});
  });

  Run({"get", "key"});
  EXPECT_LT((absl::Now() - start), absl::Milliseconds(10));
  Run({"set", "key", "value2"});
  EXPECT_GT((absl::Now() - start), absl::Milliseconds(50));
  fb1.Join();
}

}  // namespace dfly
