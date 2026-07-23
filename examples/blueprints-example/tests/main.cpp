#include "../script/scriptSerializerTest.h"
#include "documentOperationsTests.h"
#include "runtimeTests.h"
#include "testFramework.h"

#include <string>

int main(int argc, char** argv)
{
    const std::string outputPath = argc >= 2 ? argv[1] : "serialization-roundtrip-test.vlox";
    Tests::Runner runner;
    AddRuntimeTests(runner);
    AddDocumentOperationsTests(runner);
    AddScriptSerializerTests(runner, outputPath);
    return runner.Finish();
}
