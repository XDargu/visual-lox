#include "graphs/idgeneration.h"
#include "graphs/nodeRegistry.h"
#include "runtime/scriptRuntime.h"
#include "runtime/standardLibrary.h"
#include "script/scriptSerializer.h"

#include <Object.h>
#include <Vm.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
enum class BenchmarkMode
{
    Execute,
    Compile,
    Run
};

struct Options
{
    std::string scriptPath;
    std::string benchmarkName;
    std::string variant = "default";
    std::string checksumVariable = "BenchmarkChecksum";
    std::optional<uint64_t> size;
    int warmup = 3;
    int repeat = 10;
    BenchmarkMode mode = BenchmarkMode::Execute;
    bool enableConstantFolding = true;
    bool enableGarbageCollection = false;
    bool requireChecksum = true;
    bool disassemble = false;
    bool csv = false;
};

struct Measurement
{
    int64_t elapsedNanoseconds = 0;
    std::string checksum;
};

constexpr uint64_t MaxExactInteger = 9007199254740992ULL;

void PrintUsage()
{
    std::cout
        << "Visual Lox benchmark runner\n\n"
        << "Usage:\n"
        << "  vlox-benchmark [options] <script.vlox>\n\n"
        << "Options:\n"
        << "  --benchmark NAME       Result name; defaults to the script filename.\n"
        << "  --variant NAME         Variant label written to results (default: default).\n"
        << "  --size N               Override the BenchmarkSize script global.\n"
        << "  --warmup N             Untimed iterations (default: 3).\n"
        << "  --repeat N             Measured iterations (default: 10).\n"
        << "  --mode MODE            execute, compile, or run (default: execute).\n"
        << "  --folding on|off       Enable constant folding (default: on).\n"
        << "  --gc on|off            Enable garbage collection (default: off).\n"
        << "  --checksum NAME        Checksum global name (default: BenchmarkChecksum).\n"
        << "  --no-checksum          Do not read or verify a checksum.\n"
        << "  --disassemble          Print bytecode during the initial compilation only.\n"
        << "  --csv                  Write one CSV row per measured iteration.\n"
        << "  -h, --help             Show this help.\n";
}

std::string RequireValue(int& index, int argc, char** argv, const std::string& option)
{
    if (index + 1 >= argc)
        throw std::invalid_argument(option + " requires a value.");
    return argv[++index];
}

int ParseNonNegativeInt(const std::string& text, const std::string& option)
{
    size_t consumed = 0;
    const long long parsed = std::stoll(text, &consumed);
    if (consumed != text.size() || parsed < 0 || parsed > std::numeric_limits<int>::max())
        throw std::invalid_argument(option + " must be a non-negative integer.");
    return static_cast<int>(parsed);
}

uint64_t ParseSize(const std::string& text)
{
    size_t consumed = 0;
    const unsigned long long parsed = std::stoull(text, &consumed);
    if (consumed != text.size() || parsed > MaxExactInteger)
        throw std::invalid_argument("--size must be an integer between 0 and 2^53.");
    return static_cast<uint64_t>(parsed);
}

bool ParseToggle(const std::string& text, const std::string& option)
{
    if (text == "on")
        return true;
    if (text == "off")
        return false;
    throw std::invalid_argument(option + " must be 'on' or 'off'.");
}

BenchmarkMode ParseMode(const std::string& text)
{
    if (text == "execute")
        return BenchmarkMode::Execute;
    if (text == "compile")
        return BenchmarkMode::Compile;
    if (text == "run")
        return BenchmarkMode::Run;
    throw std::invalid_argument("--mode must be 'execute', 'compile', or 'run'.");
}

const char* ModeName(BenchmarkMode mode)
{
    switch (mode)
    {
    case BenchmarkMode::Execute: return "execute";
    case BenchmarkMode::Compile: return "compile";
    case BenchmarkMode::Run: return "run";
    }
    return "unknown";
}

Options ParseOptions(int argc, char** argv)
{
    Options options;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "-h" || argument == "--help")
        {
            PrintUsage();
            std::exit(0);
        }
        if (argument == "--benchmark")
            options.benchmarkName = RequireValue(index, argc, argv, argument);
        else if (argument == "--variant")
            options.variant = RequireValue(index, argc, argv, argument);
        else if (argument == "--size")
            options.size = ParseSize(RequireValue(index, argc, argv, argument));
        else if (argument == "--warmup")
            options.warmup = ParseNonNegativeInt(RequireValue(index, argc, argv, argument), argument);
        else if (argument == "--repeat")
            options.repeat = ParseNonNegativeInt(RequireValue(index, argc, argv, argument), argument);
        else if (argument == "--mode")
            options.mode = ParseMode(RequireValue(index, argc, argv, argument));
        else if (argument == "--folding")
            options.enableConstantFolding = ParseToggle(RequireValue(index, argc, argv, argument), argument);
        else if (argument == "--gc")
            options.enableGarbageCollection = ParseToggle(RequireValue(index, argc, argv, argument), argument);
        else if (argument == "--checksum")
            options.checksumVariable = RequireValue(index, argc, argv, argument);
        else if (argument == "--no-checksum")
            options.requireChecksum = false;
        else if (argument == "--disassemble")
            options.disassemble = true;
        else if (argument == "--csv")
            options.csv = true;
        else if (!argument.empty() && argument[0] == '-')
            throw std::invalid_argument("Unknown option: " + argument);
        else if (options.scriptPath.empty())
            options.scriptPath = argument;
        else
            throw std::invalid_argument("Only one script path may be supplied.");
    }

    if (options.scriptPath.empty())
        throw std::invalid_argument("A .vlox script path is required.");
    if (options.repeat < 1)
        throw std::invalid_argument("--repeat must be at least 1.");
    if (options.benchmarkName.empty())
        options.benchmarkName = std::filesystem::path(options.scriptPath).stem().string();
    return options;
}

ScriptPropertyPtr FindScriptVariable(Script& script, const std::string& name)
{
    const auto found = std::find_if(script.variables.begin(), script.variables.end(), [&](const ScriptPropertyPtr& property)
    {
        return property && property->Name == name;
    });
    return found != script.variables.end() ? *found : nullptr;
}

std::string ResolveSize(Script& script, const Options& options)
{
    const ScriptPropertyPtr sizeVariable = FindScriptVariable(script, "BenchmarkSize");
    if (options.size)
    {
        if (!sizeVariable)
            throw std::runtime_error("The script does not define the BenchmarkSize global required by --size.");
        sizeVariable->defaultValue = Value(static_cast<double>(*options.size));
        return std::to_string(*options.size);
    }

    if (!sizeVariable)
        return "";
    if (!isNumber(sizeVariable->defaultValue))
        throw std::runtime_error("BenchmarkSize must have a numeric default value.");

    const double size = asNumber(sizeVariable->defaultValue);
    if (!std::isfinite(size) || size < 0.0 || std::floor(size) != size)
        throw std::runtime_error("BenchmarkSize must be a finite, non-negative integer.");

    std::array<char, 32> buffer;
    const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), size, std::chars_format::general);
    if (converted.ec != std::errc())
        throw std::runtime_error("Could not format BenchmarkSize.");
    return std::string(buffer.data(), converted.ptr);
}

std::string FormatValue(const Value& value)
{
    if (isNil(value))
        return "nil";
    if (isBoolean(value))
        return asBoolean(value) ? "true" : "false";
    if (isNumber(value))
    {
        std::array<char, 32> buffer;
        const auto converted = std::to_chars(
            buffer.data(), buffer.data() + buffer.size(), asNumber(value), std::chars_format::general, std::numeric_limits<double>::max_digits10);
        if (converted.ec != std::errc())
            throw std::runtime_error("Could not format the numeric checksum.");
        return std::string(buffer.data(), converted.ptr);
    }
    return valueAsStr(value);
}

std::string ReadChecksum(VM& vm, const std::string& variableName)
{
    Value value;
    ObjString* name = copyString(variableName.c_str(), static_cast<int>(variableName.size()));
    if (!vm.globalTable().get(name, &value))
        throw std::runtime_error("The script did not define the checksum global '" + variableName + "'.");
    return FormatValue(value);
}

void PrintDiagnostics(const ScriptCompileResult& result)
{
    for (const ValidationDiagnostic& diagnostic : result.validation.diagnostics)
    {
        std::ostream& output = diagnostic.severity == DiagnosticSeverity::Error ? std::cerr : std::clog;
        output << FormatDiagnostic(diagnostic) << '\n';
    }
}

ScriptCompileResult Compile(VM& vm, const Script& script, const Options& options, bool initial)
{
    ScriptCompileOptions compileOptions;
    compileOptions.enableConstantFolding = options.enableConstantFolding;
    compileOptions.disassemble = initial && options.disassemble;
    ScriptCompileResult result = ScriptRuntime::Compile(vm, script, compileOptions);
    if (initial || !result)
        PrintDiagnostics(result);
    if (!result)
        throw std::runtime_error("Visual Lox compilation failed.");
    return result;
}

void Execute(VM& vm, ObjFunction* function)
{
    const InterpretResult result = ScriptRuntime::Execute(vm, function);
    if (result == InterpretResult::INTERPRET_RUNTIME_ERROR)
        throw std::runtime_error("Visual Lox execution failed with a runtime error.");
    if (result != InterpretResult::INTERPRET_OK)
        throw std::runtime_error("Visual Lox execution failed.");
}

template<typename Function>
int64_t MeasureNanoseconds(Function&& function)
{
    const auto started = std::chrono::steady_clock::now();
    function();
    const auto finished = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count();
}

std::string CsvField(const std::string& text)
{
    if (text.find_first_of(",\"\r\n") == std::string::npos)
        return text;

    std::string escaped = "\"";
    for (const char character : text)
    {
        escaped += character;
        if (character == '"')
            escaped += '"';
    }
    escaped += '"';
    return escaped;
}

double Median(std::vector<int64_t> values)
{
    std::sort(values.begin(), values.end());
    const size_t middle = values.size() / 2;
    if (values.size() % 2 != 0)
        return static_cast<double>(values[middle]);
    return (static_cast<double>(values[middle - 1]) + static_cast<double>(values[middle])) / 2.0;
}

void WriteResults(const Options& options, const std::string& size, const std::vector<Measurement>& measurements)
{
    if (options.csv)
    {
        std::cout << "benchmark,language,variant,size,iteration,time_ns,checksum\n";
        for (size_t index = 0; index < measurements.size(); ++index)
        {
            const Measurement& measurement = measurements[index];
            std::cout << CsvField(options.benchmarkName) << ",vlox," << CsvField(options.variant) << ',' << CsvField(size) << ',' << index + 1 << ','
                      << measurement.elapsedNanoseconds << ',' << CsvField(measurement.checksum) << '\n';
        }
        return;
    }

    std::vector<int64_t> elapsed;
    elapsed.reserve(measurements.size());
    for (const Measurement& measurement : measurements)
        elapsed.push_back(measurement.elapsedNanoseconds);
    std::sort(elapsed.begin(), elapsed.end());
    const size_t p95Index = std::max<size_t>(0, static_cast<size_t>(std::ceil(elapsed.size() * 0.95)) - 1);

    std::cout << "benchmark=" << options.benchmarkName << " language=vlox variant=" << options.variant << " size=" << size
              << " mode=" << ModeName(options.mode) << " gc=" << (options.enableGarbageCollection ? "on" : "off")
              << " folding=" << (options.enableConstantFolding ? "on" : "off");
    if (!measurements.front().checksum.empty())
        std::cout << " checksum=" << measurements.front().checksum;
    std::cout << '\n';
    std::cout << std::fixed << std::setprecision(6) << "runs=" << measurements.size() << " median_ms=" << Median(elapsed) / 1'000'000.0
              << " min_ms=" << elapsed.front() / 1'000'000.0 << " p95_ms=" << elapsed[p95Index] / 1'000'000.0 << '\n';
}
}

int main(int argc, char** argv)
{
    try
    {
        const Options options = ParseOptions(argc, argv);

        VM& vm = VM::getInstance();
        NodeRegistry registry;
        RegisterStandardLibrary(registry);
        registry.RegisterNatives(vm);

        Script script;
        IDGenerator ids;
        ObjFunction* pinnedFunction = nullptr;
        vm.setExternalMarkingFunc([&]()
        {
            MarkNodeRegistryRoots(registry, vm);
            ScriptUtils::MarkScriptRoots(script);
            if (pinnedFunction)
                vm.markObject(pinnedFunction);
        });
        vm.allowGarbageCollection(options.enableGarbageCollection);

        const SerializationResult loadResult = ScriptSerializer::Load(options.scriptPath, registry, script, ids);
        if (!loadResult)
            throw std::runtime_error("Could not load '" + options.scriptPath + "': " + loadResult.error);
        const std::string size = ResolveSize(script, options);

        std::vector<Measurement> measurements;
        measurements.reserve(static_cast<size_t>(options.repeat));
        std::string expectedChecksum;

        if (options.mode == BenchmarkMode::Execute)
        {
            ScriptCompileResult compiled = Compile(vm, script, options, true);
            pinnedFunction = compiled.function;
            Execute(vm, pinnedFunction);
            if (options.requireChecksum)
                expectedChecksum = ReadChecksum(vm, options.checksumVariable);

            for (int iteration = 0; iteration < options.warmup; ++iteration)
            {
                Execute(vm, pinnedFunction);
                if (options.requireChecksum && ReadChecksum(vm, options.checksumVariable) != expectedChecksum)
                    throw std::runtime_error("Benchmark checksum changed during warm-up.");
            }

            for (int iteration = 0; iteration < options.repeat; ++iteration)
            {
                Measurement measurement;
                measurement.elapsedNanoseconds = MeasureNanoseconds([&]() { Execute(vm, pinnedFunction); });
                if (options.requireChecksum)
                {
                    measurement.checksum = ReadChecksum(vm, options.checksumVariable);
                    if (measurement.checksum != expectedChecksum)
                        throw std::runtime_error("Benchmark checksum changed during a measured iteration.");
                }
                measurements.push_back(std::move(measurement));
            }
        }
        else if (options.mode == BenchmarkMode::Compile)
        {
            ScriptCompileResult compiled = Compile(vm, script, options, true);
            pinnedFunction = compiled.function;

            for (int iteration = 0; iteration < options.warmup; ++iteration)
            {
                compiled = Compile(vm, script, options, false);
                pinnedFunction = compiled.function;
            }

            for (int iteration = 0; iteration < options.repeat; ++iteration)
            {
                Measurement measurement;
                measurement.elapsedNanoseconds = MeasureNanoseconds([&]()
                {
                    compiled = Compile(vm, script, options, false);
                    pinnedFunction = compiled.function;
                });
                measurements.push_back(std::move(measurement));
            }
        }
        else
        {
            const auto compileAndExecute = [&](bool initial)
            {
                ScriptCompileResult compiled = Compile(vm, script, options, initial);
                pinnedFunction = compiled.function;
                Execute(vm, pinnedFunction);
            };

            compileAndExecute(true);
            if (options.requireChecksum)
                expectedChecksum = ReadChecksum(vm, options.checksumVariable);

            for (int iteration = 0; iteration < options.warmup; ++iteration)
            {
                compileAndExecute(false);
                if (options.requireChecksum && ReadChecksum(vm, options.checksumVariable) != expectedChecksum)
                    throw std::runtime_error("Benchmark checksum changed during warm-up.");
            }

            for (int iteration = 0; iteration < options.repeat; ++iteration)
            {
                Measurement measurement;
                measurement.elapsedNanoseconds = MeasureNanoseconds([&]() { compileAndExecute(false); });
                if (options.requireChecksum)
                {
                    measurement.checksum = ReadChecksum(vm, options.checksumVariable);
                    if (measurement.checksum != expectedChecksum)
                        throw std::runtime_error("Benchmark checksum changed during a measured iteration.");
                }
                measurements.push_back(std::move(measurement));
            }
        }

        WriteResults(options, size, measurements);
        vm.setExternalMarkingFunc([]() {});
        return 0;
    }
    catch (const std::invalid_argument& error)
    {
        std::cerr << "Argument error: " << error.what() << "\n\n";
        PrintUsage();
        return 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Benchmark error: " << error.what() << '\n';
        return 3;
    }
}
