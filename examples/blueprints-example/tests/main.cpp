#include "../script/scriptSerializerTest.h"
#include "runtimeTests.h"
#include "documentOperationsTests.h"

#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    const std::string outputPath = argc >= 2 ? argv[1] : "serialization-roundtrip-test.vlox";
    const int runtimeResult = RunRuntimeTests();
    const int operationsResult = runtimeResult == 0 ? RunDocumentOperationsTests() : runtimeResult;
    const int serializationResult = operationsResult == 0
        ? RunScriptSerializerRoundTripTest(outputPath)
        : operationsResult;
    const int result = runtimeResult != 0 ? runtimeResult
        : (operationsResult != 0 ? operationsResult : serializationResult);
    if (result != 0)
        std::cerr << "Visual Lox tests failed. See " << outputPath << ".log\n";
    return result;
}
