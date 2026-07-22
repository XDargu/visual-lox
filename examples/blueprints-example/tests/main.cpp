#include "../script/scriptSerializerTest.h"
#include "runtimeTests.h"

#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    const std::string outputPath = argc >= 2 ? argv[1] : "serialization-roundtrip-test.vlox";
    const int runtimeResult = RunRuntimeTests();
    const int serializationResult = runtimeResult == 0
        ? RunScriptSerializerRoundTripTest(outputPath)
        : runtimeResult;
    const int result = runtimeResult != 0 ? runtimeResult : serializationResult;
    if (result != 0)
        std::cerr << "Visual Lox tests failed. See " << outputPath << ".log\n";
    return result;
}
