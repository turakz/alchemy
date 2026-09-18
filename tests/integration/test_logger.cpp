// tests/integration/test_logger.cpp
// integration tests for the dual-output logger

// std
#include <cstdio>

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// 3rd party
#include <gtest/gtest.h>

// local
#include "logger/logger.hpp"
#include "utils.hpp"

class LoggerIntegrationTest : public ::testing::Test {
protected:
  void
  SetUp() override
  {
    tempDir =
        alchemy::testing::utils::createTempTestDirectory("alchemy_logger_test");
    logFile = tempDir / "test.log";
  }

  void
  TearDown() override
  {
    alchemy::logger::shutdown();
    if (std::filesystem::exists(tempDir))
    {
      std::filesystem::remove_all(tempDir);
    }
  }

  std::string
  readFile(const std::filesystem::path& path)
  {
    std::ifstream file(path);
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
  }

  std::size_t
  countLines(const std::string& text)
  {
    if (text.empty())
    {
      return 0;
    }
    std::size_t count = 0;
    for (auto ch : text)
    {
      if (ch == '\n')
      {
        ++count;
      }
    }
    return count;
  }

  std::filesystem::path tempDir;
  std::filesystem::path logFile;
};

// -- console routing tests --

TEST_F(LoggerIntegrationTest, InfoWritesToStdout)
{
  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();

  alchemy::logger::info("hello_info\n");

  std::string stdoutOutput = testing::internal::GetCapturedStdout();
  std::string stderrOutput = testing::internal::GetCapturedStderr();

  EXPECT_NE(stdoutOutput.find("hello_info"), std::string::npos);
  EXPECT_EQ(stderrOutput.find("hello_info"), std::string::npos);
}

TEST_F(LoggerIntegrationTest, WarnWritesToStdout)
{
  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();

  alchemy::logger::warn("hello_warn\n");

  std::string stdoutOutput = testing::internal::GetCapturedStdout();
  std::string stderrOutput = testing::internal::GetCapturedStderr();

  EXPECT_NE(stdoutOutput.find("hello_warn"), std::string::npos);
  EXPECT_EQ(stderrOutput.find("hello_warn"), std::string::npos);
}

TEST_F(LoggerIntegrationTest, ErrorWritesToStderr)
{
  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();

  alchemy::logger::error("hello_error\n");

  std::string stdoutOutput = testing::internal::GetCapturedStdout();
  std::string stderrOutput = testing::internal::GetCapturedStderr();

  EXPECT_EQ(stdoutOutput.find("hello_error"), std::string::npos);
  EXPECT_NE(stderrOutput.find("hello_error"), std::string::npos);
}

TEST_F(LoggerIntegrationTest, DebugIsSilentOnConsole)
{
  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();

  alchemy::logger::debug("hello_debug\n");

  std::string stdoutOutput = testing::internal::GetCapturedStdout();
  std::string stderrOutput = testing::internal::GetCapturedStderr();

  EXPECT_EQ(stdoutOutput.find("hello_debug"), std::string::npos);
  EXPECT_EQ(stderrOutput.find("hello_debug"), std::string::npos);
}

// -- log file output tests --

TEST_F(LoggerIntegrationTest, AllLevelsWrittenToFileWhenDebugEnabled)
{
  alchemy::logger::setLevel(alchemy::logger::Level::Debug);
  alchemy::logger::init(logFile);

  // suppress console output during test
  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();

  alchemy::logger::debug("msg_debug\n");
  alchemy::logger::info("msg_info\n");
  alchemy::logger::warn("msg_warn\n");
  alchemy::logger::error("msg_error\n");

  testing::internal::GetCapturedStdout();
  testing::internal::GetCapturedStderr();

  alchemy::logger::shutdown();

  std::string content = readFile(logFile);

  EXPECT_NE(content.find("[DEBUG]"), std::string::npos);
  EXPECT_NE(content.find("[INFO]"), std::string::npos);
  EXPECT_NE(content.find("[WARN]"), std::string::npos);
  EXPECT_NE(content.find("[ERROR]"), std::string::npos);
  EXPECT_NE(content.find("msg_debug"), std::string::npos);
  EXPECT_NE(content.find("msg_info"), std::string::npos);
  EXPECT_NE(content.find("msg_warn"), std::string::npos);
  EXPECT_NE(content.find("msg_error"), std::string::npos);
}

TEST_F(LoggerIntegrationTest, TimestampsPresent)
{
  alchemy::logger::init(logFile);

  testing::internal::CaptureStdout();
  alchemy::logger::info("timestamp_test\n");
  testing::internal::GetCapturedStdout();

  alchemy::logger::shutdown();

  std::string content = readFile(logFile);

  // match [YYYY-MM-DD HH:MM:SS]
  std::regex timestampPattern(R"(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\])");
  EXPECT_TRUE(std::regex_search(content, timestampPattern));
}

TEST_F(LoggerIntegrationTest, AnsiCodesStripped)
{
  alchemy::logger::init(logFile);

  testing::internal::CaptureStdout();
  alchemy::logger::info("pre\033[1;32mgreen\033[0mpost\n");
  testing::internal::GetCapturedStdout();

  alchemy::logger::shutdown();

  std::string content = readFile(logFile);

  EXPECT_NE(content.find("pregreenpost"), std::string::npos);
  EXPECT_EQ(content.find("\033"), std::string::npos);
}

TEST_F(LoggerIntegrationTest, NoFileOutputBeforeInit)
{
  // do not call init — log should only go to console
  testing::internal::CaptureStdout();
  alchemy::logger::info("before_init\n");
  std::string stdoutOutput = testing::internal::GetCapturedStdout();

  EXPECT_NE(stdoutOutput.find("before_init"), std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(logFile));
}

TEST_F(LoggerIntegrationTest, CreatesParentDirectories)
{
  auto nestedLog = tempDir / "nested" / "deep" / "test.log";
  alchemy::logger::init(nestedLog);

  testing::internal::CaptureStdout();
  alchemy::logger::info("nested_msg\n");
  testing::internal::GetCapturedStdout();

  alchemy::logger::shutdown();

  ASSERT_TRUE(std::filesystem::exists(nestedLog));
  std::string content = readFile(nestedLog);
  EXPECT_NE(content.find("nested_msg"), std::string::npos);
}

// -- thread safety --

TEST_F(LoggerIntegrationTest, ConcurrentWritesDoNotCorruptWhenDebugEnabled)
{
  alchemy::logger::setLevel(alchemy::logger::Level::Debug);
  alchemy::logger::init(logFile);

  static constexpr int NumThreads = 8;
  static constexpr int MessagesPerThread = 100;

  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();

  std::vector<std::thread> threads;
  for (int t = 0; t < NumThreads; ++t)
  {
    threads.emplace_back([t]() {
      for (int m = 0; m < MessagesPerThread; ++m)
      {
        alchemy::logger::debug("thread_{}_msg_{}\n", t, m);
      }
    });
  }

  for (auto& thread : threads)
  {
    thread.join();
  }

  testing::internal::GetCapturedStdout();
  testing::internal::GetCapturedStderr();

  alchemy::logger::shutdown();

  std::string content = readFile(logFile);
  std::size_t lineCount = countLines(content);

  EXPECT_EQ(lineCount, NumThreads * MessagesPerThread);

  // verify each thread's messages appear
  for (int t = 0; t < NumThreads; ++t)
  {
    std::string marker = "thread_" + std::to_string(t) + "_msg_0";
    EXPECT_NE(content.find(marker), std::string::npos)
        << "missing messages from thread " << t;
  }
}

// -- init idempotence --

TEST_F(LoggerIntegrationTest, DoubleInitDoesNotTruncate)
{
  alchemy::logger::init(logFile);

  testing::internal::CaptureStdout();
  alchemy::logger::info("first_msg\n");
  testing::internal::GetCapturedStdout();

  // second init should be a no-op
  alchemy::logger::init(logFile);

  testing::internal::CaptureStdout();
  alchemy::logger::info("second_msg\n");
  testing::internal::GetCapturedStdout();

  alchemy::logger::shutdown();

  std::string content = readFile(logFile);
  EXPECT_NE(content.find("first_msg"), std::string::npos);
  EXPECT_NE(content.find("second_msg"), std::string::npos);
}

// -- formatList utility --

TEST_F(LoggerIntegrationTest, FormatListEmpty)
{
  std::vector<std::string> empty;
  EXPECT_EQ(alchemy::logger::formatList(empty), "[]");
}

TEST_F(LoggerIntegrationTest, FormatListSingle)
{
  std::vector<std::string> items = {"alpha"};
  EXPECT_EQ(alchemy::logger::formatList(items), "[alpha]");
}

TEST_F(LoggerIntegrationTest, FormatListMultiple)
{
  std::vector<std::string> items = {"a", "b", "c"};
  EXPECT_EQ(alchemy::logger::formatList(items), "[a, b, c]");
}
