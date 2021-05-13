/*
 ____  _____ _        _
| __ )| ____| |      / \
|  _ \|  _| | |     / _ \
| |_) | |___| |___ / ___ \
|____/|_____|_____/_/   \_\
http://bela.io
*/

#include <Bela.h>
#include <libraries/Trill/Trill.h>
#include <libraries/Gui/Gui.h>
#include <libraries/Pipe/Pipe.h>
#include <libraries/Trill/CentroidDetection.h>

#define NUM_TOUCH 4 // Number of touches on our custom slider

Trill touchSensor;

Pipe gPipe;
Gui gui;

CentroidDetection cd;
// Location of touches on Trill
float gTouchLocation[NUM_TOUCH] = {0.0};
// Size of touches on Trill
float gTouchSize[NUM_TOUCH] = {0.0};
// Number of active touches
int gNumActiveTouches = 0;


// Sleep time for auxiliary task
unsigned int gTaskSleepTime = 12000; // microseconds

// Time period (in seconds) after which data will be sent to the GUI
float gTimePeriod = 0.015;

int bitResolution = 12;

int gButtonValue = 0;

typedef enum {
	kPrescaler,
	kBaseline,
	kNoiseThreshold,
	kNumBits,
	kMode,
} ids_t;
struct Command {
	ids_t id;
	float value;
};

#include <tuple>
std::vector<std::pair<std::wstring, ids_t>> gKeys =
{
	{L"prescaler", kPrescaler},
	{L"baseline", kBaseline},
	{L"noiseThreshold", kNoiseThreshold},
	{L"numBits", kNumBits},
	{L"mode", kMode},
};

// This callback is called every time a new message is received from the Gui.
// Given how we cannot operate on the touchSensor object from a separate
// thread, we need to pipe the received messages to the loop() thread, so that
// they can be processed there
bool guiCallback(JSONObject& json, void*)
{
	struct Command command;
	for(auto& k : gKeys)
	{
		if(json.find(k.first) != json.end() && json[k.first]->IsNumber())
		{
			command.id = k.second;
			command.value = json[k.first]->AsNumber();
			gPipe.writeNonRt(command);
		}
	}
	return false;
}

void loop(void*)
{
	int numBits;
	int speed = 0;
	while(!Bela_stopRequested())
	{
		touchSensor.readI2C();
		cd.process(touchSensor.rawData.data());
		for(unsigned int i = 0; i < cd.getNumTouches() ; i++) {
			gTouchLocation[i] = cd.touchLocation(i);
			gTouchSize[i] = cd.touchSize(i);
		}
		gNumActiveTouches = cd.getNumTouches();
		/* you could use compound touch instead
		gTouchLocation[0] = cd.compoundTouchLocation();
		gTouchSize[0] = cd.compoundTouchSize();
		gNumActiveTouches = !(!gTouchSize[0]);
		*/

		Command command;
		// receive any command from the gui through the pipe
		while(1 == gPipe.readRt(command))
		{
			float value = command.value;
			switch(command.id)
			{
				case kPrescaler:
					printf("setting prescaler to %.0f\n", value);
					touchSensor.setPrescaler(value);
					break;
				case kBaseline:
					printf("reset baseline\n");
					touchSensor.updateBaseline();
					break;
				case kNoiseThreshold:
					printf("setting noiseThreshold to %f\n", value);
					touchSensor.setNoiseThreshold(value);
					break;
				case kNumBits:
					numBits = value;
					printf("setting number of bits to %d\n", numBits);
					touchSensor.setScanSettings(speed, numBits);
					break;
				case kMode:
					printf("setting mode to %.0f\n", value);
					touchSensor.setMode((Trill::Mode)value);
					break;
			}
		}
		usleep(50000);
	}
}

bool setup(BelaContext *context, void *userData)
{
	// Setup a Trill Flex on i2c bus 1, using the default address.
	if(touchSensor.setup(1, Trill::RING) != 0) {
		fprintf(stderr, "Unable to initialise Trill Flex\n");
		return false;
	}
	// set it to DIFF mode: we will only use some pads from it, which we set up next
	touchSensor.setMode(Trill::DIFF);
	// specify which pads to use (and in which order), how many touches and
	// the rescaling divisor for centroid size (depends on the shape and
	// material of your sensing surface)
	cd.setup(28, NUM_TOUCH, 3200);
	cd.setWrapAround(5);

	gui.setup(context->projectName);
	gui.setControlDataCallback(guiCallback, nullptr);
	gPipe.setup("guiToLoop");

	Bela_runAuxiliaryTask(loop);
	return true;
}

void render(BelaContext *context, void *userData)
{
	static unsigned int count = 0;

	for(unsigned int n = 0; n < context->audioFrames; n++) {
		// Send number of touches, touch location and size to the GUI
		// after some time has elapsed.
		if(count >= gTimePeriod*context->audioSampleRate)
		{
			gui.sendBuffer(0, touchSensor.getNumChannels());
			gui.sendBuffer(1, touchSensor.rawData);
			gui.sendBuffer(2, gNumActiveTouches);
			gui.sendBuffer(3, gTouchLocation);
			gui.sendBuffer(4, gTouchSize);
			count = 0;
		}
		count++;
	}
}

void cleanup(BelaContext *context, void *userData)
{
}
