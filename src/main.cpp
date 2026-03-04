// src/main.cpp

// std
#include <cstdint>
#include <cstdlib>

#include <exception>
#include <utility>

// 3rd party
#include <fmt/core.h>

// local
#include "app/app.hpp"
#include "app/color.hpp"
#include "app/core/core.hpp"
#include "cli/cli.hpp"

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
      fmt::print(stderr, "{}\n", std::move(cliArgs).error());
      return EXIT_FAILURE;
    }

    // create app with parsed options
    alchemy::core::Result<alchemy::App> appResult =
        alchemy::App::create(std::move(cliArgs).value());

    if (appResult.invalid())
    {
      fmt::print(stderr, "{}\n", std::move(appResult).error());
      return EXIT_FAILURE;
    }

    // run alchemy -> transmute!
    auto app = std::move(appResult).value();
    const std::int32_t Status = app.exec();

    if (Status == EXIT_SUCCESS)
    {
      fmt::print("alchemy::{}executed successfully{}: yer a wizard, 'arry!",
                 alchemy::color::ansi::BoldBrightGreen,
                 alchemy::color::ansi::Reset,
                 alchemy::color::ansi::BoldBrightGreen,
                 alchemy::color::ansi::Reset);
    }
    // error messages already printed by App::exec()

    return Status;
  }
  catch (const std::exception& e)
  {
    fmt::print(stderr,
               "alchemy::{}fatal{}::exception: {}\n",
               alchemy::color::ansi::BoldMagenta,
               alchemy::color::ansi::Reset,
               e.what());
    return EXIT_FAILURE;
  }
}
