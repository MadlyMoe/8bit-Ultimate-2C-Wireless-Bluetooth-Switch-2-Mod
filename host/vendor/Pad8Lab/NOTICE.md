Windows HID, normal application and recovery protocol sources are adapted from [JehuAlv/Pad8Lab](https://github.com/JehuAlv/Pad8Lab/tree/14b55c42565c85a90287799e399adaa7cc3204a9), from the upstream Hid and Protocol directories under src/Pad8Lab.Core. The upstream GPL license is included alongside this notice.

The local BootProtocol copy omits the upstream erase, write and device-classification convenience methods. ControllerRead uses the read-only API. ControllerFlash implements its bounded write and restart path in host-flash, using the upstream packet format and device transport.
