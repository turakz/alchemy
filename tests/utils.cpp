// std
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "app/app.hpp"
#include "app/core/core.hpp"
#include "cli/cli.hpp"
#include "operation/operation_base.hpp"
#include "parsing/artifacts/artifacts.hpp"

// 3rd party

// local
#include <cstddef>

#include "utils.hpp"

std::filesystem::path
alchemy::testing::utils::createTestFile(const std::filesystem::path& directory,
                                        const std::string& filename,
                                        const std::string& content)
{
  std::filesystem::path filePath = directory / filename;
  std::filesystem::create_directories(filePath.parent_path());
  std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
  if (!file)
  {
    throw std::runtime_error(
        "alchemy::testing::utils::createTestFile: failed to create file: " +
        filePath.string());
  }
  file << content;
  if (!file)
  {
    throw std::runtime_error(
        "alchemy::testing::utils::createTestFile: failed to write to file: " +
        filePath.string());
  }
  file.close();
  return filePath;
}

std::string
alchemy::testing::utils::readFile(const std::filesystem::path& filePath)
{
  std::ifstream file(filePath, std::ios::binary);
  if (!file)
  {
    throw std::runtime_error(
        "alchemy::testing::utils::readFile: failed to open file: " +
        filePath.string());
  }
  return std::string{std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>()};
}

void
alchemy::testing::utils::createSyntheticCompilationDatabase(
    const std::filesystem::path& directory,
    const std::vector<std::string>& sourceFiles)
{
  // create compile_commands.json directly in the directory where source files
  // are
  const std::filesystem::path CompileCommandsPath =
      directory / "compile_commands.json";
  std::ofstream compileCommands(CompileCommandsPath);

  compileCommands << "[\n";

  // if no source files provided, create a default entry
  if (sourceFiles.empty())
  {
    compileCommands << "  {\n";
    compileCommands << R"(    "directory": ")" << directory.string() << R"(",)"
                    << "\n";
    compileCommands << R"(    "command": "clang -c test_file.h",)" << "\n";
    compileCommands << R"(    "file": "test_file.h")" << "\n";
    compileCommands << "  }\n";
  }
  else
  {
    // create an entry for each source file
    for (size_t i = 0; i < sourceFiles.size(); ++i)
    {
      const std::filesystem::path SourcePath(sourceFiles[i]);
      const std::string Filename = SourcePath.filename().string();
      const std::string AbsolutePath = SourcePath.string();

      compileCommands << "  {\n";
      compileCommands << R"(    "directory": ")" << directory.string()
                      << R"(",)" << "\n";
      compileCommands << R"(    "command": "clang -c )" << AbsolutePath
                      << R"(",)" << "\n";
      compileCommands << R"(    "file": ")" << AbsolutePath << R"(")" << "\n";
      compileCommands << "  }";

      if (i < sourceFiles.size() - 1)
      {
        compileCommands << ",";
      }
      compileCommands << "\n";
    }
  }

  compileCommands << "]\n";
  compileCommands.flush();
  compileCommands.close();
}

alchemy::operation::Recipe
alchemy::testing::utils::createRecipe(const std::filesystem::path& sourceFile,
                                      std::size_t byteOffset,
                                      std::size_t byteLength,
                                      const std::string& replacementText)
{
  alchemy::operation::detail::RefactorRecipe recipe;
  recipe.sourceFile = sourceFile;
  recipe.byteOffset = byteOffset;
  recipe.byteLength = byteLength;
  recipe.replacementText = replacementText;
  return recipe;
}

alchemy::parser::artifacts::StructDef
alchemy::testing::utils::createStructDef(
    const std::string& structName,
    const std::filesystem::path& sourceFile,
    const std::vector<alchemy::parser::artifacts::FieldDef>& fields,
    std::size_t naturalAlignment)
{
  alchemy::parser::artifacts::StructDef structDef(structName, sourceFile);

  for (const auto& field : fields)
  {
    structDef.fields.push_back(field);
  }

  structDef.naturalAlignment = naturalAlignment;

  // Use actual production methods (don't duplicate logic!)
  structDef.currentDataSize =
      alchemy::parser::artifacts::StructDef::computeDataSize(fields);
  structDef.naturalTotalSize =
      alchemy::parser::artifacts::StructDef::computeSize(fields,
                                                         naturalAlignment);
  structDef.currentWastedBytes =
      structDef.naturalTotalSize - structDef.currentDataSize;

  return structDef;
}

alchemy::parser::artifacts::StructDef
alchemy::testing::utils::createStructDefExplicit(
    const std::string& structName,
    const std::filesystem::path& sourceFile,
    const std::vector<alchemy::parser::artifacts::FieldDef>& fields,
    std::size_t naturalAlignment,
    std::size_t currentDataSize,
    std::size_t naturalTotalSize,
    std::size_t currentWastedBytes)
{
  alchemy::parser::artifacts::StructDef structDef(structName, sourceFile);

  for (const auto& field : fields)
  {
    structDef.fields.push_back(field);
  }

  structDef.naturalAlignment = naturalAlignment;
  structDef.currentDataSize = currentDataSize;
  structDef.naturalTotalSize = naturalTotalSize;
  structDef.currentWastedBytes = currentWastedBytes;

  return structDef;
}

alchemy::parser::artifacts::FieldDef
alchemy::testing::utils::createFieldDef(unsigned byteOffset,
                                        unsigned byteLength,
                                        const std::string& typeName,
                                        const std::string& fieldName,
                                        std::size_t naturalSize,
                                        std::size_t naturalAlignment,
                                        bool isBitField,
                                        bool canReorder)
{
  alchemy::parser::artifacts::FieldDef field(byteOffset,
                                             byteLength,
                                             typeName,
                                             fieldName,
                                             naturalSize,
                                             naturalAlignment);
  field.isBitField = isBitField;
  field.canReorder = canReorder;
  return field;
}

// create CliInputs for Validator testing
alchemy::cli::CliInputs
alchemy::testing::utils::createCliInputs(const MockCliConfig& config)
{
  alchemy::cli::CliInputs inputs;
  inputs.paths.buildDir = config.buildDir.string();
  inputs.paths.outputDir = config.outputDir.string();
  inputs.paths.sourcePatterns = config.sourceFiles;
  inputs.paths.excludePatterns = config.excludePatterns;
  inputs.features.enableSalign = config.enableSalign;
  inputs.enableDryRun = config.enableDryRun;
  inputs.jobs = config.jobs;
  return inputs;
}

// struct-based API (bypasses validation for integration tests)
alchemy::cli::ParsedOptions
alchemy::testing::utils::createMockOptions(const MockCliConfig& config)
{
  alchemy::cli::ParsedOptions options;
  options.sourcePatterns = config.sourceFiles;
  options.excludePatterns = config.excludePatterns;
  options.buildDir = config.buildDir;
  options.outputDir = config.outputDir;
  options.enableSalign = config.enableSalign;
  options.enableDryRun = config.enableDryRun;
  options.jobs = config.jobs;
  return options;
}

alchemy::core::Result<alchemy::App>
alchemy::testing::utils::createAlchemyWithMockOptions(
    const MockCliConfig& config)
{
  auto options = createMockOptions(config);
  return alchemy::App::create(std::move(options));
}

void
alchemy::testing::utils::createGCCDatabase(
    const std::filesystem::path& buildDir,
    const std::filesystem::path& projectRoot)
{
  auto mainFile = (projectRoot / "main.c").string();
  auto configFile = (projectRoot / "src" / "config.c").string();
  auto utilsFile = (projectRoot / "src" / "utils.c").string();
  auto incDir = (projectRoot / "inc").string();
  auto buildDirStr = buildDir.string();

  const std::string Database =
      R"([
{
  "directory": ")" +
      buildDirStr + R"(",
  "command": "/usr/bin/cc -I)" +
      incDir +
      R"( -Wall -Wpedantic -Wshadow -Wfloat-equal -Wundef -Wcast-align -O0 -g -std=c17 -o CMakeFiles/SharedProject.dir/main.c.o -c )" +
      mainFile + R"(",
  "file": ")" +
      mainFile + R"("
},
{
  "directory": ")" +
      buildDirStr + R"(",
  "command": "/usr/bin/cc -I)" +
      incDir +
      R"( -Wall -Wpedantic -Wshadow -Wfloat-equal -Wundef -Wcast-align -O0 -g -std=c17 -o CMakeFiles/SharedProject.dir/src/config.c.o -c )" +
      configFile + R"(",
  "file": ")" +
      configFile + R"("
},
{
  "directory": ")" +
      buildDirStr + R"(",
  "command": "/usr/bin/cc -I)" +
      incDir +
      R"( -Wall -Wpedantic -Wshadow -Wfloat-equal -Wundef -Wcast-align -O0 -g -std=c17 -o CMakeFiles/SharedProject.dir/src/utils.c.o -c )" +
      utilsFile + R"(",
  "file": ")" +
      utilsFile + R"("
}
])";

  std::ofstream out(buildDir / "compile_commands.json");
  out << Database;
  out.close();
}

void
alchemy::testing::utils::createClangDatabase(
    const std::filesystem::path& buildDir,
    const std::filesystem::path& projectRoot)
{
  auto mainFile = (projectRoot / "main.c").string();
  auto configFile = (projectRoot / "src" / "config.c").string();
  auto utilsFile = (projectRoot / "src" / "utils.c").string();
  auto incDir = (projectRoot / "inc").string();
  auto buildDirStr = buildDir.string();

  const std::string Database =
      R"([
{
  "directory": ")" +
      buildDirStr + R"(",
  "command": "clang -I)" +
      incDir +
      R"( -Wall -Wpedantic -Wshadow -Wfloat-equal -Wundef -Wcast-align -O0 -g -std=c17 -o CMakeFiles/SharedProject.dir/main.c.o -c )" +
      mainFile + R"(",
  "file": ")" +
      mainFile + R"("
},
{
  "directory": ")" +
      buildDirStr + R"(",
  "command": "clang -I)" +
      incDir +
      R"( -Wall -Wpedantic -Wshadow -Wfloat-equal -Wundef -Wcast-align -O0 -g -std=c17 -o CMakeFiles/SharedProject.dir/src/config.c.o -c )" +
      configFile + R"(",
  "file": ")" +
      configFile + R"("
},
{
  "directory": ")" +
      buildDirStr + R"(",
  "command": "clang -I)" +
      incDir +
      R"( -Wall -Wpedantic -Wshadow -Wfloat-equal -Wundef -Wcast-align -O0 -g -std=c17 -o CMakeFiles/SharedProject.dir/src/utils.c.o -c )" +
      utilsFile + R"(",
  "file": ")" +
      utilsFile + R"("
}
])";

  std::ofstream out(buildDir / "compile_commands.json");
  out << Database;
  out.close();
}

void
alchemy::testing::utils::createIARDatabase(
    const std::filesystem::path& buildDir,
    const std::filesystem::path& projectRoot)
{
  auto mainFile = (projectRoot / "main.c").string();
  auto configFile = (projectRoot / "src" / "config.c").string();
  auto utilsFile = (projectRoot / "src" / "utils.c").string();
  auto incDir = (projectRoot / "inc").string();
  auto buildDirStr = buildDir.string();

  // Use mock IAR compiler for testing (from build/tests/data/mock_compilers/)
  // Integration tests run from source root, so construct path to build
  // directory
  auto mockCompiler = std::filesystem::absolute(
                          std::filesystem::current_path() / "build" / "tests" /
                          "data" / "mock_compilers" / "mock_iccarm")
                          .string();

  const std::string Database = R"([
{
  "directory": ")" + buildDirStr +
                               R"(",
  "command": ")" + mockCompiler +
                               R"( --cpu=Cortex-M4 --dlib_config normal -I)" +
                               incDir + R"( -c )" + mainFile +
                               R"( -o build/main.o",
  "file": ")" + mainFile +
                               R"("
},
{
  "directory": ")" + buildDirStr +
                               R"(",
  "command": ")" + mockCompiler +
                               R"( --cpu=Cortex-M4 --dlib_config normal -I)" +
                               incDir + R"( -c )" + configFile +
                               R"( -o build/config.o",
  "file": ")" + configFile +
                               R"("
},
{
  "directory": ")" + buildDirStr +
                               R"(",
  "command": ")" + mockCompiler +
                               R"( --cpu=Cortex-M4 --dlib_config normal -I)" +
                               incDir + R"( -c )" + utilsFile +
                               R"( -o build/utils.o",
  "file": ")" + utilsFile +
                               R"("
}
])";

  std::ofstream out(buildDir / "compile_commands.json");
  out << Database;
  out.close();
}

void
alchemy::testing::utils::createMSVCDatabase(
    const std::filesystem::path& buildDir,
    const std::filesystem::path& projectRoot)
{
  auto mainFile = (projectRoot / "main.c").string();
  auto configFile = (projectRoot / "src" / "config.c").string();
  auto utilsFile = (projectRoot / "src" / "utils.c").string();
  auto incDir = (projectRoot / "inc").string();
  auto buildDirStr = buildDir.string();

  const std::string Database =
      R"([
{
  "directory": ")" +
      buildDirStr + R"(",
  "command": "cl.exe /I)" +
      incDir + R"( /c )" + mainFile + R"( /Fobuild\\main.obj",
  "file": ")" +
      mainFile + R"("
},
{
  "directory": ")" +
      buildDirStr + R"(",
  "command": "cl.exe /I)" +
      incDir + R"( /c )" + configFile + R"( /Fobuild\\config.obj",
  "file": ")" +
      configFile + R"("
},
{
  "directory": ")" +
      buildDirStr + R"(",
  "command": "cl.exe /I)" +
      incDir + R"( /c )" + utilsFile + R"( /Fobuild\\utils.obj",
  "file": ")" +
      utilsFile + R"("
}
])";

  std::ofstream out(buildDir / "compile_commands.json");
  out << Database;
  out.close();
}

void
alchemy::testing::utils::copyDirectory(const std::filesystem::path& src,
                                       const std::filesystem::path& dst)
{
  std::filesystem::create_directories(dst);
  for (const auto& entry : std::filesystem::recursive_directory_iterator(src))
  {
    const auto& path = entry.path();
    auto relativePath = std::filesystem::relative(path, src);
    auto destPath = dst / relativePath;

    if (std::filesystem::is_directory(path))
    {
      std::filesystem::create_directories(destPath);
    }
    else
    {
      std::filesystem::copy_file(
          path, destPath, std::filesystem::copy_options::overwrite_existing);
    }
  }
}
