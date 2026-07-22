#include "../script/scriptSerializerTest.h"

#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    const std::string outputPath = argc >= 2 ? argv[1] : "serialization-roundtrip-test.vlox";
    const int result = RunScriptSerializerRoundTripTest(outputPath);
    if (result != 0)
        std::cerr << "Visual Lox tests failed. See " << outputPath << ".log\n";
    return result;
}
