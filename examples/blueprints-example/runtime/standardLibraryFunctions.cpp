#include "standardLibraryFunctions.h"

#include <Object.h>
#include <Vm.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace
{
bool NumberArgs(Value* args, int count)
{
    for (int i = 0; i < count; ++i)
        if (!isNumber(args[i]))
            return false;
    return true;
}

std::string Trim(std::string value)
{
    const auto whitespace = [](unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), whitespace));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), whitespace).base(), value.end());
    return value;
}

void ReplaceAll(std::string& text, const std::string& search,
                const std::string& replacement)
{
    if (search.empty())
        return;
    size_t position = 0;
    while ((position = text.find(search, position)) != std::string::npos)
    {
        text.replace(position, search.size(), replacement);
        position += replacement.size();
    }
}

ObjList* BeginPackage(VM* vm)
{
    ObjList* package = newList();
    vm->push(Value(package));
    return package;
}

Value EndPackage(VM* vm, ObjList* package)
{
    vm->pop();
    return Value(package);
}

Value StringValue(std::string value)
{
    return Value(takeString(std::move(value)));
}

Value FileResult(VM* vm, bool success, const std::string& error)
{
    ObjList* result = BeginPackage(vm);
    result->append(Value(success));
    result->append(StringValue(error));
    return EndPackage(vm, result);
}

bool ValueLess(const Value& left, const Value& right)
{
    if (isNumber(left) && isNumber(right))
        return asNumber(left) < asNumber(right);
    if (isString(left) && isString(right))
        return asString(left)->chars < asString(right)->chars;
    if (isBoolean(left) && isBoolean(right))
        return !asBoolean(left) && asBoolean(right);
    if (isNil(left) != isNil(right))
        return isNil(left);
    return valueAsStr(left) < valueAsStr(right);
}
}

Value MathAbs(int, Value* args, VM*)
{
    return isNumber(args[0]) ? Value(std::fabs(asNumber(args[0]))) : Value();
}

Value MathMin(int, Value* args, VM*)
{
    return NumberArgs(args, 2) ? Value(std::min(asNumber(args[0]), asNumber(args[1]))) : Value();
}

Value MathMax(int, Value* args, VM*)
{
    return NumberArgs(args, 2) ? Value(std::max(asNumber(args[0]), asNumber(args[1]))) : Value();
}

Value MathClamp(int, Value* args, VM*)
{
    if (!NumberArgs(args, 3))
        return Value();
    double minimum = asNumber(args[1]);
    double maximum = asNumber(args[2]);
    if (minimum > maximum)
        std::swap(minimum, maximum);
    return Value(std::clamp(asNumber(args[0]), minimum, maximum));
}

Value MathPower(int, Value* args, VM*)
{
    return NumberArgs(args, 2) ? Value(std::pow(asNumber(args[0]), asNumber(args[1]))) : Value();
}

Value MathSqrt(int, Value* args, VM*)
{
    if (!isNumber(args[0]) || asNumber(args[0]) < 0.0)
        return Value();
    return Value(std::sqrt(asNumber(args[0])));
}

Value MathFloor(int, Value* args, VM*)
{
    return isNumber(args[0]) ? Value(std::floor(asNumber(args[0]))) : Value();
}

Value MathCeil(int, Value* args, VM*)
{
    return isNumber(args[0]) ? Value(std::ceil(asNumber(args[0]))) : Value();
}

Value MathRound(int, Value* args, VM*)
{
    return isNumber(args[0]) ? Value(std::round(asNumber(args[0]))) : Value();
}

Value MathRandom(int, Value* args, VM*)
{
    if (!NumberArgs(args, 2))
        return Value();
    double minimum = asNumber(args[0]);
    double maximum = asNumber(args[1]);
    if (minimum > maximum)
        std::swap(minimum, maximum);
    static thread_local std::mt19937 generator(std::random_device{}());
    return Value(std::uniform_real_distribution<double>(minimum, maximum)(generator));
}

Value StringTrim(int, Value* args, VM*)
{
    return isString(args[0]) ? StringValue(Trim(asString(args[0])->chars)) : Value();
}

Value StringReplace(int, Value* args, VM*)
{
    if (!isString(args[0]) || !isString(args[1]) || !isString(args[2]))
        return Value();
    std::string result = asString(args[0])->chars;
    ReplaceAll(result, asString(args[1])->chars, asString(args[2])->chars);
    return StringValue(std::move(result));
}

Value StringJoin(int, Value* args, VM*)
{
    if (!isList(args[0]) || !isString(args[1]))
        return Value();
    const std::string& separator = asString(args[1])->chars;
    std::string result;
    const std::vector<Value>& items = asList(args[0])->items;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (i > 0)
            result += separator;
        result += valueAsStr(items[i]);
    }
    return StringValue(std::move(result));
}

Value StringStartsWith(int, Value* args, VM*)
{
    if (!isString(args[0]) || !isString(args[1]))
        return Value();
    const std::string& text = asString(args[0])->chars;
    const std::string& prefix = asString(args[1])->chars;
    return Value(text.size() >= prefix.size() &&
                 text.compare(0, prefix.size(), prefix) == 0);
}

Value StringEndsWith(int, Value* args, VM*)
{
    if (!isString(args[0]) || !isString(args[1]))
        return Value();
    const std::string& text = asString(args[0])->chars;
    const std::string& suffix = asString(args[1])->chars;
    return Value(text.size() >= suffix.size() &&
                 text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0);
}

Value StringFormat(int, Value* args, VM*)
{
    if (!isString(args[0]) || !isList(args[1]))
        return Value();
    std::string result = asString(args[0])->chars;
    const std::vector<Value>& values = asList(args[1])->items;
    for (size_t i = 0; i < values.size(); ++i)
        ReplaceAll(result, "{" + std::to_string(i) + "}", valueAsStr(values[i]));
    return StringValue(std::move(result));
}

Value StringParseNumber(int, Value* args, VM* vm)
{
    ObjList* result = BeginPackage(vm);
    double value = 0.0;
    bool success = false;
    if (isString(args[0]))
    {
        const std::string text = Trim(asString(args[0])->chars);
        char* end = nullptr;
        errno = 0;
        value = std::strtod(text.c_str(), &end);
        success = !text.empty() && end == text.c_str() + text.size() &&
                  errno != ERANGE && std::isfinite(value);
    }
    result->append(Value(value));
    result->append(Value(success));
    return EndPackage(vm, result);
}

Value StringParseBool(int, Value* args, VM* vm)
{
    ObjList* result = BeginPackage(vm);
    bool value = false;
    bool success = false;
    if (isString(args[0]))
    {
        std::string text = Trim(asString(args[0])->chars);
        std::transform(text.begin(), text.end(), text.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (text == "true" || text == "1")
        {
            value = true;
            success = true;
        }
        else if (text == "false" || text == "0")
        {
            success = true;
        }
    }
    result->append(Value(value));
    result->append(Value(success));
    return EndPackage(vm, result);
}

Value ListInsert(int, Value* args, VM*)
{
    if (!isList(args[0]) || !isNumber(args[1]))
        return Value();
    ObjList* list = asList(args[0]);
    const int index = static_cast<int>(asNumber(args[1]));
    if (index < 0 || index > static_cast<int>(list->items.size()))
        return Value();
    list->items.insert(list->items.begin() + index, args[2]);
    return Value(static_cast<double>(list->items.size()));
}

Value ListClear(int, Value* args, VM*)
{
    if (!isList(args[0]))
        return Value();
    asList(args[0])->items.clear();
    return Value(0.0);
}

Value ListSlice(int, Value* args, VM* vm)
{
    if (!isList(args[0]) || !NumberArgs(args + 1, 2))
        return Value();
    const std::vector<Value>& source = asList(args[0])->items;
    const int start = std::max(0, static_cast<int>(asNumber(args[1])));
    const int count = std::max(0, static_cast<int>(asNumber(args[2])));
    ObjList* result = newList();
    vm->push(Value(result));
    const int end = std::min(
        static_cast<int>(source.size()), start + count);
    for (int i = start; i < end; ++i)
        result->append(source[i]);
    vm->pop();
    return Value(result);
}

Value ListReverse(int, Value* args, VM* vm)
{
    if (!isList(args[0]))
        return Value();
    ObjList* result = newList();
    vm->push(Value(result));
    result->items = asList(args[0])->items;
    std::reverse(result->items.begin(), result->items.end());
    vm->pop();
    return Value(result);
}

Value ListSort(int, Value* args, VM* vm)
{
    if (!isList(args[0]))
        return Value();
    ObjList* result = newList();
    vm->push(Value(result));
    result->items = asList(args[0])->items;
    std::stable_sort(result->items.begin(), result->items.end(), ValueLess);
    vm->pop();
    return Value(result);
}

Value ListDistinct(int, Value* args, VM* vm)
{
    if (!isList(args[0]))
        return Value();
    ObjList* result = newList();
    vm->push(Value(result));
    for (const Value& value : asList(args[0])->items)
    {
        if (std::find(result->items.begin(), result->items.end(), value) ==
            result->items.end())
            result->append(value);
    }
    vm->pop();
    return Value(result);
}

Value ListEnumerate(int, Value* args, VM* vm)
{
    if (!isList(args[0]))
        return Value();
    ObjList* result = newList();
    vm->push(Value(result));
    const std::vector<Value>& source = asList(args[0])->items;
    for (size_t i = 0; i < source.size(); ++i)
    {
        ObjList* item = newList();
        item->append(Value(static_cast<double>(i)));
        item->append(source[i]);
        result->append(Value(item));
    }
    vm->pop();
    return Value(result);
}

Value ListZip(int, Value* args, VM* vm)
{
    if (!isList(args[0]) || !isList(args[1]))
        return Value();
    ObjList* result = newList();
    vm->push(Value(result));
    const std::vector<Value>& left = asList(args[0])->items;
    const std::vector<Value>& right = asList(args[1])->items;
    const size_t count = std::min(left.size(), right.size());
    for (size_t i = 0; i < count; ++i)
    {
        ObjList* item = newList();
        item->append(left[i]);
        item->append(right[i]);
        result->append(Value(item));
    }
    vm->pop();
    return Value(result);
}

Value RangeMakeAdvanced(int, Value* args, VM*)
{
    if (!NumberArgs(args, 3) || !isBoolean(args[3]) || !isBoolean(args[4]) ||
        asNumber(args[2]) == 0.0)
        return Value();
    return Value(newRange(asNumber(args[0]), asNumber(args[1]), asNumber(args[2]),
                          asBoolean(args[3]), asBoolean(args[4])));
}

Value FileReadText(int, Value* args, VM* vm)
{
    ObjList* result = BeginPackage(vm);
    std::string content;
    bool success = false;
    std::string error;
    if (!isString(args[0]))
    {
        error = "Path must be a string.";
    }
    else
    {
        std::ifstream stream(asString(args[0])->chars, std::ios::binary);
        if (!stream)
            error = "Could not open file for reading.";
        else
        {
            std::ostringstream buffer;
            buffer << stream.rdbuf();
            content = buffer.str();
            success = stream.good() || stream.eof();
            if (!success)
                error = "Could not read the complete file.";
        }
    }
    result->append(StringValue(std::move(content)));
    result->append(Value(success));
    result->append(StringValue(std::move(error)));
    return EndPackage(vm, result);
}

Value FileWriteText(int, Value* args, VM* vm)
{
    if (!isString(args[0]) || !isString(args[1]))
        return FileResult(vm, false, "Path and content must be strings.");
    std::ofstream stream(asString(args[0])->chars, std::ios::binary | std::ios::trunc);
    if (!stream)
        return FileResult(vm, false, "Could not open file for writing.");
    stream << asString(args[1])->chars;
    return FileResult(vm, static_cast<bool>(stream),
                      stream ? "" : "Could not write the complete file.");
}

Value FileAppendText(int, Value* args, VM* vm)
{
    if (!isString(args[0]) || !isString(args[1]))
        return FileResult(vm, false, "Path and content must be strings.");
    std::ofstream stream(asString(args[0])->chars, std::ios::binary | std::ios::app);
    if (!stream)
        return FileResult(vm, false, "Could not open file for appending.");
    stream << asString(args[1])->chars;
    return FileResult(vm, static_cast<bool>(stream),
                      stream ? "" : "Could not append the complete content.");
}

Value FileListDirectory(int, Value* args, VM* vm)
{
    ObjList* package = BeginPackage(vm);
    ObjList* entries = newList();
    package->append(Value(entries));
    bool success = false;
    std::string error;
    if (!isString(args[0]))
    {
        error = "Path must be a string.";
    }
    else
    {
        std::error_code code;
        std::vector<std::string> paths;
        for (std::filesystem::directory_iterator iterator(
                 asString(args[0])->chars, code), end;
             !code && iterator != end; iterator.increment(code))
            paths.push_back(iterator->path().string());
        if (code)
            error = code.message();
        else
        {
            std::sort(paths.begin(), paths.end());
            for (std::string& path : paths)
                entries->append(StringValue(std::move(path)));
            success = true;
        }
    }
    package->append(Value(success));
    package->append(StringValue(std::move(error)));
    return EndPackage(vm, package);
}

Value PathCombine(int, Value* args, VM*)
{
    if (!isString(args[0]) || !isString(args[1]))
        return Value();
    return StringValue((std::filesystem::path(asString(args[0])->chars) /
                        asString(args[1])->chars).lexically_normal().string());
}

Value PathExtension(int, Value* args, VM*)
{
    return isString(args[0])
        ? StringValue(std::filesystem::path(asString(args[0])->chars).extension().string())
        : Value();
}

Value PathFilename(int, Value* args, VM*)
{
    return isString(args[0])
        ? StringValue(std::filesystem::path(asString(args[0])->chars).filename().string())
        : Value();
}

Value PathParent(int, Value* args, VM*)
{
    return isString(args[0])
        ? StringValue(std::filesystem::path(asString(args[0])->chars).parent_path().string())
        : Value();
}
