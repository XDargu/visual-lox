#include "generator.h"

#include "../../examples/blueprints-example/runtime/standardLibrary.h"

#include <array>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace
{

using CreateExample = Script (*)(const NodeRegistry&);
using SmokeTest = void (*)(VM&, const NodeRegistry&, const std::filesystem::path&);

struct ExampleDefinition
{
    const char* id;
    const char* filename;
    CreateExample create;
    SmokeTest smokeTest;
};

constexpr std::array<ExampleDefinition, 3> Examples = {{
    { "rock-paper-scissors", "RockPaperScissors.vlox", ExampleGenerator::MakeRockPaperScissors, nullptr },
    { "game-of-life", "GameOfLife.vlox", ExampleGenerator::MakeGameOfLife, ExampleGenerator::SmokeTestGameOfLife },
    { "mandelbrot", "Mandelbrot.vlox", ExampleGenerator::MakeMandelbrot, ExampleGenerator::SmokeTestMandelbrot },
}};

struct Options
{
    std::filesystem::path outputDirectory = "programs";
    std::optional<std::string> exampleId;
    bool list = false;
    bool help = false;
};

void PrintUsage()
{
    std::cout
        << "Usage: vlox-example-generator [output-directory] [example-id]\n"
        << "       vlox-example-generator [--output <directory>] [--example <id>]\n"
        << "       vlox-example-generator --list\n\n"
        << "With no example ID, all registered examples are generated.\n";
}

void PrintExampleIds()
{
    for (const ExampleDefinition& example : Examples)
        std::cout << example.id << '\n';
}

Options ParseOptions(int argc, char** argv)
{
    Options options;
    bool positionalOutputSet = false;

    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h")
        {
            options.help = true;
        }
        else if (argument == "--list")
        {
            options.list = true;
        }
        else if (argument == "--output")
        {
            if (++index >= argc)
                throw std::runtime_error("--output requires a directory.");
            options.outputDirectory = argv[index];
            positionalOutputSet = true;
        }
        else if (argument == "--example")
        {
            if (++index >= argc)
                throw std::runtime_error("--example requires an example ID.");
            options.exampleId = argv[index];
        }
        else if (!argument.empty() && argument[0] == '-')
        {
            throw std::runtime_error("Unknown option: " + argument);
        }
        else if (!positionalOutputSet)
        {
            options.outputDirectory = argument;
            positionalOutputSet = true;
        }
        else if (!options.exampleId)
        {
            options.exampleId = argument;
        }
        else
        {
            throw std::runtime_error("Unexpected argument: " + argument);
        }
    }

    return options;
}

const ExampleDefinition* FindExample(const std::string& id)
{
    for (const ExampleDefinition& example : Examples)
        if (id == example.id)
            return &example;
    return nullptr;
}

void GenerateExample(const ExampleDefinition& definition, VM& vm, const NodeRegistry& registry, const std::filesystem::path& outputDirectory)
{
    const std::filesystem::path output = outputDirectory / definition.filename;
    Script script = definition.create(registry);
    ExampleGenerator::ValidateCompileAndSave(vm, registry, script, output);
    if (definition.smokeTest)
        definition.smokeTest(vm, registry, output);
}

}

int main(int argc, char** argv)
{
    try
    {
        const Options options = ParseOptions(argc, argv);
        if (options.help)
        {
            PrintUsage();
            return 0;
        }
        if (options.list)
        {
            PrintExampleIds();
            return 0;
        }

        const ExampleDefinition* selected = nullptr;
        if (options.exampleId)
        {
            selected = FindExample(*options.exampleId);
            if (!selected)
            {
                std::cerr << "Unknown example ID '" << *options.exampleId << "'. Available IDs:\n";
                PrintExampleIds();
                return 2;
            }
        }

        std::filesystem::create_directories(options.outputDirectory);

        VM& vm = VM::getInstance();
        NodeRegistry registry;
        RegisterStandardLibrary(registry);
        RegisterVisualApplicationLibrary(registry);
        registry.RegisterNatives(vm);

        size_t generatedCount = 0;
        if (selected)
        {
            GenerateExample(*selected, vm, registry, options.outputDirectory);
            generatedCount = 1;
        }
        else
        {
            for (const ExampleDefinition& example : Examples)
            {
                GenerateExample(example, vm, registry, options.outputDirectory);
                ++generatedCount;
            }
        }

        std::cout << "Generated " << generatedCount << " example" << (generatedCount == 1 ? "" : "s") << ".\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Generation error: " << error.what() << '\n';
        return 1;
    }
}
