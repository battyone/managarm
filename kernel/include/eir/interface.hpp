
static const uint64_t eirSignatureValue = 0x68692C2074686F72;

static const uint32_t eirDebugSerial = 1;
static const uint32_t eirDebugBochs = 2;

typedef uint64_t EirPtr;
typedef uint64_t EirSize;

struct EirRegion {
	EirPtr address;
	EirSize length;
	EirSize order; // TODO: This could be an int.
	EirSize numRoots;
	EirPtr buddyTree;
};

struct EirModule {
	EirPtr physicalBase;
	EirSize length;
	EirPtr namePtr;
	EirSize nameLength;
};

struct EirFramebuffer {
	EirPtr fbAddress;
	EirPtr fbEarlyWindow;
	EirSize fbPitch;
	EirSize fbWidth;
	EirSize fbHeight;
	EirSize fbBpp;
	EirSize fbType;
};

struct EirInfo {
	uint64_t signature;
	EirPtr commandLine;
	uint32_t debugFlags;
	uint32_t padding;

	EirRegion coreRegion;

	EirSize numModules;
	EirPtr moduleInfo;

	EirFramebuffer frameBuffer;
};

