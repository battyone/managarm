
struct PciDevice {
	enum BarType {
		kBarNone = 0,
		kBarIo = 1,
		kBarMemory = 2
	};

	struct Bar {
		Bar()
		: type(kBarNone), handle(kHelNullHandle), length(0) { }

		BarType type;
		HelHandle handle;
		size_t length;
	};

	PciDevice(uint32_t bus, uint32_t slot, uint32_t function,
			uint32_t vendor, uint32_t device_id, uint8_t revision,
			uint8_t class_code, uint8_t sub_class, uint8_t interface)
	: bus(bus), slot(slot), function(function),
			vendor(vendor), deviceId(device_id), revision(revision),
			classCode(class_code), subClass(sub_class), interface(interface) { }
	
	// location of the device on the PCI bus
	uint32_t bus;
	uint32_t slot;
	uint32_t function;
	
	// vendor-specific device information
	uint16_t vendor;
	uint16_t deviceId;
	uint8_t revision;

	// generic device information
	uint8_t classCode;
	uint8_t subClass;
	uint8_t interface;
	
	// device configuration
	Bar bars[6];
};

enum {
	// general PCI header fields
	kPciVendor = 0,
	kPciDevice = 2,
	kPciRevision = 0x08,
	kPciInterface = 0x09,
	kPciSubClass = 0x0A,
	kPciClassCode = 0x0B,
	kPciHeaderType = 0x0E,

	// usual device header fields
	kPciBar0 = 0x10,

	// PCI-to-PCI bridge header fields
	kPciSecondaryBus = 0x19
};

// read from pci configuration space
uint32_t readPciWord(uint32_t bus, uint32_t slot, uint32_t function, uint32_t offset);
uint16_t readPciHalf(uint32_t bus, uint32_t slot, uint32_t function, uint32_t offset);
uint8_t readPciByte(uint32_t bus, uint32_t slot, uint32_t function, uint32_t offset);

// write to pci configuration space
void writePciWord(uint32_t bus, uint32_t slot, uint32_t function, uint32_t offset, uint32_t value);
