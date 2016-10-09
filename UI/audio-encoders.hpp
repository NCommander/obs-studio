#include <obs.hpp>
#include <string>
#include <map>
using namespace std;
const std::map<int, const char*> &GetAACEncoderBitrateMap(string codecName);
const char *GetAACEncoderForBitrate(int bitrate, string codecName);
int FindClosestAvailableAACBitrate(int bitrate, string codecName);
