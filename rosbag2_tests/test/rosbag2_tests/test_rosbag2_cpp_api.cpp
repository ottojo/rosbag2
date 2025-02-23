// Copyright 2020, Bosch Software Innovations GmbH.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gmock/gmock.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "rclcpp/clock.hpp"
#include "rclcpp/serialization.hpp"
#include "rclcpp/serialized_message.hpp"

#include "rcutils/time.h"

#include "rosbag2_cpp/reader.hpp"
#include "rosbag2_cpp/readers/sequential_reader.hpp"
#include "rosbag2_cpp/writer.hpp"
#include "rosbag2_cpp/writers/sequential_writer.hpp"

#include "rosbag2_test_common/tested_storage_ids.hpp"

#include "test_msgs/msg/basic_types.hpp"

using namespace ::testing;  // NOLINT

namespace fs = std::filesystem;

class TestRosbag2CPPAPI : public Test, public WithParamInterface<std::string>
{};

TEST_P(TestRosbag2CPPAPI, minimal_writer_example)
{
  using TestMsgT = test_msgs::msg::BasicTypes;
  TestMsgT test_msg;
  test_msg.float64_value = 12345.6789;
  rclcpp::SerializedMessage serialized_msg;

  rclcpp::Serialization<TestMsgT> serialization;
  serialization.serialize_message(&test_msg, &serialized_msg);

  auto rosbag_directory = fs::path("test_rosbag2_writer_api_bag");
  auto rosbag_directory_next = fs::path("test_rosbag2_writer_api_bag_next");
  // in case the bag was previously not cleaned up
  fs::remove_all(rosbag_directory);
  fs::remove_all(rosbag_directory_next);

  {
    rosbag2_cpp::Writer writer;
    rosbag2_storage::StorageOptions storage_options;
    storage_options.storage_id = GetParam();
    storage_options.uri = rosbag_directory.string();
    writer.open(storage_options);

    auto bag_message = std::make_shared<rosbag2_storage::SerializedBagMessage>();
    auto ret = rcutils_system_time_now(&bag_message->recv_timestamp);
    if (ret != RCL_RET_OK) {
      FAIL() << "couldn't assign time rosbag message";
    }

    rosbag2_storage::TopicMetadata tm;
    tm.name = "/my/test/topic";
    tm.type = "test_msgs/msg/BasicTypes";
    tm.serialization_format = "cdr";
    writer.create_topic(tm);

    bag_message->topic_name = tm.name;
    bag_message->serialized_data = std::shared_ptr<rcutils_uint8_array_t>(
      &serialized_msg.get_rcl_serialized_message(), [](rcutils_uint8_array_t * /* data */) {});

    writer.write(bag_message);

    // alternative way of writing a message
    // if there's a topic mismatch, we throw
    EXPECT_ANY_THROW(writer.write(bag_message, "/my/other/topic", "test_msgs/msg/BasicTypes"));

    bag_message->topic_name = "/my/other/topic";
    writer.write(bag_message, "/my/other/topic", "test_msgs/msg/BasicTypes");

    // yet another alternative, writing the rclcpp::SerializedMessage directly
    std::shared_ptr<rclcpp::SerializedMessage> serialized_msg2 =
      std::make_shared<rclcpp::SerializedMessage>();
    serialization.serialize_message(&test_msg, serialized_msg2.get());

    writer.write(
      serialized_msg2, "/yet/another/topic", "test_msgs/msg/BasicTypes",
      rclcpp::Clock().now());

    // writing a non-serialized message
    writer.write(test_msg, "/a/ros2/message", rclcpp::Clock().now());

    // close as prompted
    writer.close();

    // open a new bag with the same writer
    writer.open(rosbag_directory_next.string());

    // write same topic to different bag
    writer.write(
      serialized_msg2, "/yet/another/topic", "test_msgs/msg/BasicTypes",
      rclcpp::Clock().now());

    // close by scope
  }

  {
    rosbag2_cpp::Reader reader;
    reader.open(rosbag_directory.string());
    std::vector<std::string> topics;
    while (reader.has_next()) {
      auto bag_message = reader.read_next();
      topics.push_back(bag_message->topic_name);

      TestMsgT extracted_test_msg;
      rclcpp::SerializedMessage extracted_serialized_msg(*bag_message->serialized_data);
      serialization.deserialize_message(
        &extracted_serialized_msg, &extracted_test_msg);

      EXPECT_EQ(test_msg, extracted_test_msg);
    }
    ASSERT_EQ(4u, topics.size());
    EXPECT_EQ("/my/test/topic", topics[0]);
    EXPECT_EQ("/my/other/topic", topics[1]);
    EXPECT_EQ("/yet/another/topic", topics[2]);
    EXPECT_EQ("/a/ros2/message", topics[3]);

    // close on scope exit
  }

  {
    rosbag2_cpp::Reader reader;
    std::string topic;
    reader.open(rosbag_directory_next.string());
    ASSERT_TRUE(reader.has_next());

    auto bag_message = reader.read_next();
    topic = bag_message->topic_name;

    TestMsgT extracted_test_msg;
    rclcpp::SerializedMessage extracted_serialized_msg(*bag_message->serialized_data);
    serialization.deserialize_message(
      &extracted_serialized_msg, &extracted_test_msg);

    EXPECT_EQ(test_msg, extracted_test_msg);
    EXPECT_EQ("/yet/another/topic", topic);
  }

  // alternative reader
  {
    rosbag2_cpp::Reader reader;
    reader.open(rosbag_directory.string());
    while (reader.has_next()) {
      TestMsgT extracted_test_msg = reader.read_next<TestMsgT>();
      EXPECT_EQ(test_msg, extracted_test_msg);
    }

    // close on scope exit
  }

  // remove the rosbag again after the test
  EXPECT_TRUE(fs::remove_all(rosbag_directory));
  EXPECT_TRUE(fs::remove_all(rosbag_directory_next));
}

INSTANTIATE_TEST_SUITE_P(
  ParametrizedRosbag2CPPAPITests,
  TestRosbag2CPPAPI,
  ValuesIn(rosbag2_test_common::kTestedStorageIDs)
);
