#pragma once
#include "pluginmain.h"
#include <algorithm>
#include <thread>
#include <vector>

//plugin data
#define PLUGIN_NAME "tracemerge"
#define PLUGIN_VERSION 1

//functions
bool pluginInit(PLUG_INITSTRUCT* initStruct);
void pluginStop();
void pluginSetup();
