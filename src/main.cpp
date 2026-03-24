// src/main.cpp

// std
#include <chrono>
#include <cstdint>
#include <cstdlib>

#include <exception>
#include <utility>

// local
#include "app/app.hpp"
#include "app/color.hpp"
#include "app/core/core.hpp"
#include "cli/cli.hpp"
#include "logger/logger.hpp"

std::int32_t
main(std::int32_t argc, const char** argv)
{
  try
  {
    // parse CLI first
    alchemy::core::Result<alchemy::cli::ParsedOptions> cliArgs =
        alchemy::cli::parseCli(argc, argv);

    if (cliArgs.invalid())
    {
      alchemy::logger::error("{}\n", std::move(cliArgs).error());
      return EXIT_FAILURE;
    }

    // init logger before app creation (discovery logs during create)
    if (cliArgs.value().enableDebug)
    {
      alchemy::logger::setLevel(alchemy::logger::Level::Debug);
      alchemy::logger::init(cliArgs.value().rootDir / ".alchemy" /
                            "alchemy.log");
    }

    const auto RunStart = std::chrono::steady_clock::now();

    // create app with parsed options
    alchemy::core::Result<alchemy::App> appResult =
        alchemy::App::create(std::move(cliArgs).value());

    if (appResult.invalid())
    {
      alchemy::logger::error("{}\n", std::move(appResult).error());
      alchemy::logger::shutdown();
      return EXIT_FAILURE;
    }

    // run alchemy -> transmute!
    auto app = std::move(appResult).value();
    const std::int32_t Status = app.exec();

    if (Status == EXIT_SUCCESS)
    {
      const auto ElapsedTime = std::chrono::duration_cast<std::chrono::seconds>(
                                   std::chrono::steady_clock::now() - RunStart)
                                   .count();
      alchemy::logger::info(
          "alchemy::{}executed successfully{}: yer a wizard, 'arry!\n",
          alchemy::color::ansi::BoldBrightGreen,
          alchemy::color::ansi::Reset);
      alchemy::logger::info("alchemy::{}elapsed time{}: {}s\n",
                            alchemy::color::ansi::BoldBrightGreen,
                            alchemy::color::ansi::Reset,
                            ElapsedTime);
    }
    // error messages already printed by App::exec()

    alchemy::logger::shutdown();
    return Status;
  }
  catch (const std::exception& e)
  {
    alchemy::logger::error("alchemy::{}fatal{}::exception: {}\n",
                           alchemy::color::ansi::BoldMagenta,
                           alchemy::color::ansi::Reset,
                           e.what());
    alchemy::logger::shutdown();
    return EXIT_FAILURE;
  }
}
