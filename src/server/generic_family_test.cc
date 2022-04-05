// Copyright 2022, Roman Gershman.  All rights reserved.
// See LICENSE for licensing terms.
//

#include "server/generic_family.h"

#include "base/gtest.h"
#include "base/logging.h"
#include "facade/facade_test.h"
#include "server/command_registry.h"
#include "server/conn_context.h"
#include "server/engine_shard_set.h"
#include "server/string_family.h"
#include "server/test_utils.h"
#include "server/transaction.h"
#include "util/uring/uring_pool.h"

using namespace testing;
using namespace std;
using namespace util;
using namespace boost;
using absl::StrCat;

namespace dfly {

class GenericFamilyTest : public BaseFamilyTest {};

TEST_F(GenericFamilyTest, Expire) {
  Run({"set", "key", "val"});
  auto resp = Run({"expire", "key", "1"});

  EXPECT_THAT(resp[0], IntArg(1));
  UpdateTime(expire_now_ + 1000);
  resp = Run({"get", "key"});
  EXPECT_THAT(resp, ElementsAre(ArgType(RespExpr::NIL)));

  Run({"set", "key", "val"});
  resp = Run({"pexpireat", "key", absl::StrCat(expire_now_ + 2000)});
  EXPECT_THAT(resp[0], IntArg(1));

  // override
  resp = Run({"pexpireat", "key", absl::StrCat(expire_now_ + 3000)});
  EXPECT_THAT(resp[0], IntArg(1));

  UpdateTime(expire_now_ + 2999);
  resp = Run({"get", "key"});
  EXPECT_THAT(resp[0], "val");

  UpdateTime(expire_now_ + 3000);
  resp = Run({"get", "key"});
  EXPECT_THAT(resp[0], ArgType(RespExpr::NIL));
}

TEST_F(GenericFamilyTest, Del) {
  for (size_t i = 0; i < 1000; ++i) {
    Run({"set", StrCat("foo", i), "1"});
    Run({"set", StrCat("bar", i), "1"});
  }

  ASSERT_EQ(2000, CheckedInt({"dbsize"}));

  auto exist_fb = pp_->at(0)->LaunchFiber([&] {
    for (size_t i = 0; i < 1000; ++i) {
      int64_t resp = CheckedInt({"exists", StrCat("foo", i), StrCat("bar", i)});
      ASSERT_TRUE(2 == resp || resp == 0) << resp << " " << i;
    }
  });

  auto del_fb = pp_->at(2)->LaunchFiber([&] {
    for (size_t i = 0; i < 1000; ++i) {
      auto resp = CheckedInt({"del", StrCat("foo", i), StrCat("bar", i)});
      ASSERT_EQ(2, resp);
    }
  });

  exist_fb.join();
  del_fb.join();
}

TEST_F(GenericFamilyTest, TTL) {
  EXPECT_EQ(-2, CheckedInt({"ttl", "foo"}));
  EXPECT_EQ(-2, CheckedInt({"pttl", "foo"}));
  Run({"set", "foo", "bar"});
  EXPECT_EQ(-1, CheckedInt({"ttl", "foo"}));
  EXPECT_EQ(-1, CheckedInt({"pttl", "foo"}));
}

TEST_F(GenericFamilyTest, Exists) {
  Run({"mset", "x", "0", "y", "1"});
  auto resp = Run({"exists", "x", "y", "x"});
  EXPECT_THAT(resp[0], IntArg(3));
}

TEST_F(GenericFamilyTest, Rename) {
  RespVec resp;
  string b_val(32, 'b');
  string x_val(32, 'x');

  resp = Run({"mset", "x", x_val, "b", b_val});
  ASSERT_THAT(resp, RespEq("OK"));
  ASSERT_EQ(2, last_cmd_dbg_info_.shards_count);

  resp = Run({"rename", "z", "b"});
  ASSERT_THAT(resp[0], ErrArg("no such key"));

  resp = Run({"rename", "x", "b"});
  ASSERT_THAT(resp, RespEq("OK"));

  int64_t val = CheckedInt({"get", "x"});
  ASSERT_EQ(kint64min, val);  // does not exist

  ASSERT_THAT(Run({"get", "b"}), RespEq(x_val));  // swapped.

  EXPECT_EQ(CheckedInt({"exists", "x", "b"}), 1);

  const char* keys[2] = {"b", "x"};
  auto ren_fb = pp_->at(0)->LaunchFiber([&] {
    for (size_t i = 0; i < 200; ++i) {
      int j = i % 2;
      auto resp = Run({"rename", keys[j], keys[1 - j]});
      ASSERT_THAT(resp, RespEq("OK"));
    }
  });

  auto exist_fb = pp_->at(2)->LaunchFiber([&] {
    for (size_t i = 0; i < 300; ++i) {
      int64_t resp = CheckedInt({"exists", "x", "b"});
      ASSERT_EQ(1, resp);
    }
  });

  exist_fb.join();
  ren_fb.join();
}

TEST_F(GenericFamilyTest, RenameNonString) {
  EXPECT_EQ(1, CheckedInt({"lpush", "x", "elem"}));
  auto resp = Run({"rename", "x", "b"});
  ASSERT_THAT(resp, RespEq("OK"));
  ASSERT_EQ(2, last_cmd_dbg_info_.shards_count);

  EXPECT_EQ(0, CheckedInt({"del", "x"}));
  EXPECT_EQ(1, CheckedInt({"del", "b"}));
}

TEST_F(GenericFamilyTest, RenameBinary) {
  const char kKey1[] = "\x01\x02\x03\x04";
  const char kKey2[] = "\x05\x06\x07\x08";

  Run({"set", kKey1, "bar"});
  Run({"rename", kKey1, kKey2});
  EXPECT_THAT(Run({"get", kKey1}), ElementsAre(ArgType(RespExpr::NIL)));
  EXPECT_THAT(Run({"get", kKey2}), RespEq("bar"));
}

}  // namespace dfly
