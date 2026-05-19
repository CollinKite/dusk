// Steam Controller -> SDL virtual gamepad bridge (iOS and tvOS).
//
// The Steam Controller streams its inputs over a custom Bluetooth LE service
// that SDL does not recognize, so it is never seen as a gamepad on Apple
// platforms. This bridge connects to that service directly, decodes the
// controller's input reports, and feeds them into an SDL virtual joystick
// described as a standard gamepad. From there the game's normal SDL gamepad
// path handles it.

#import "dusk/ios/SteamControllerBridge.h"

#import <CoreBluetooth/CoreBluetooth.h>
#include <SDL3/SDL.h>

#include <cstdint>

namespace {

// GATT identifiers for the controller's Bluetooth LE service.
NSString *const kDeviceInformationService  = @"180A";
NSString *const kControllerService         = @"100F6C32-1735-4313-B402-38567131E5F3";
NSString *const kInputCharacteristicUUID   = @"100F6C7A-1735-4313-B402-38567131E5F3";
NSString *const kRumbleCharacteristicUUID  = @"100F6CB5-1735-4313-B402-38567131E5F3";

// Length, in bytes, of one controller input report.
constexpr NSUInteger kReportLength = 45;

// Maps a bit in the controller's 32-bit button field to an SDL gamepad button.
struct ButtonMapping {
    int sourceBit;
    SDL_GamepadButton sdlButton;
};
constexpr ButtonMapping kButtonMappings[] = {
    {  0, SDL_GAMEPAD_BUTTON_SOUTH },           // A
    {  1, SDL_GAMEPAD_BUTTON_EAST },            // B
    {  2, SDL_GAMEPAD_BUTTON_WEST },            // X
    {  3, SDL_GAMEPAD_BUTTON_NORTH },           // Y
    {  5, SDL_GAMEPAD_BUTTON_RIGHT_STICK },     // R3
    {  6, SDL_GAMEPAD_BUTTON_START },           // Start
    {  9, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER },  // R1
    { 10, SDL_GAMEPAD_BUTTON_DPAD_DOWN },
    { 11, SDL_GAMEPAD_BUTTON_DPAD_RIGHT },
    { 12, SDL_GAMEPAD_BUTTON_DPAD_LEFT },
    { 13, SDL_GAMEPAD_BUTTON_DPAD_UP },
    { 14, SDL_GAMEPAD_BUTTON_BACK },            // Select
    { 15, SDL_GAMEPAD_BUTTON_LEFT_STICK },      // L3
    { 16, SDL_GAMEPAD_BUTTON_GUIDE },           // Steam
    { 19, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER },   // L1
};

// The controller reports stick Y with positive pointing up, the opposite of
// SDL's gamepad convention (positive down), so both vertical axes are flipped.
constexpr bool kInvertStickY = true;

int16_t readInt16(const uint8_t *data, int offset) {
    return static_cast<int16_t>(static_cast<uint16_t>(data[offset]) |
                                (static_cast<uint16_t>(data[offset + 1]) << 8));
}

int16_t stickAxis(int16_t value) {
    if (!kInvertStickY) {
        return value;
    }
    int inverted = -static_cast<int>(value);
    if (inverted > 32767) inverted = 32767;
    if (inverted < -32768) inverted = -32768;
    return static_cast<int16_t>(inverted);
}

// The controller reports each analog trigger as 0...32767 (0 = released).
// SDL's virtual gamepad, though, reads its trigger axes across the full Sint16
// range and treats 0 as half-pressed, so a released trigger sent as 0 would
// register as permanently half-held. Remap to -32768 (released)...32767 (full).
int16_t triggerAxis(int16_t value) {
    if (value < 0) {
        value = 0;
    }
    return static_cast<int16_t>(static_cast<int>(value) * 2 - 32768);
}

}  // namespace

// Forward declaration: SDL invokes this when the game requests rumble.
static bool steamControllerRumble(void *userdata, Uint16 lowFrequency, Uint16 highFrequency);

@interface DuskSteamControllerBridge : NSObject <CBCentralManagerDelegate, CBPeripheralDelegate>
- (void)start;
- (void)stop;
- (void)writeRumbleLow:(uint16_t)lowFrequency high:(uint16_t)highFrequency;
@end

@implementation DuskSteamControllerBridge {
    dispatch_queue_t _queue;
    CBCentralManager *_central;
    CBPeripheral *_peripheral;
    CBCharacteristic *_inputCharacteristic;
    CBCharacteristic *_rumbleCharacteristic;
    SDL_JoystickID _virtualJoystickID;
    SDL_Joystick *_virtualJoystick;
}

- (instancetype)init {
    if ((self = [super init])) {
        _queue = dispatch_queue_create("dev.twilitrealm.dusk.steamcontroller", DISPATCH_QUEUE_SERIAL);
    }
    return self;
}

- (void)start {
    if (_central) {
        return;
    }
    _central = [[CBCentralManager alloc] initWithDelegate:self queue:_queue];
}

- (void)stop {
    dispatch_async(_queue, ^{
        [self->_central stopScan];
        if (self->_peripheral) {
            [self->_central cancelPeripheralConnection:self->_peripheral];
        }
        [self detachVirtualGamepad];
        self->_peripheral = nil;
        self->_inputCharacteristic = nil;
        self->_rumbleCharacteristic = nil;
    });
}

// MARK: - Discovery

- (void)beginSearch {
    // A controller already paired with the system will not appear in a scan,
    // so check the system's connected peripherals first.
    NSArray<CBPeripheral *> *connected = [_central retrieveConnectedPeripheralsWithServices:@[
        [CBUUID UUIDWithString:kDeviceInformationService]
    ]];
    for (CBPeripheral *peripheral in connected) {
        if ([peripheral.name hasPrefix:@"Steam"]) {
            SDL_Log("SteamController: found connected controller '%s'", peripheral.name.UTF8String);
            [self connectTo:peripheral];
            return;
        }
    }
    SDL_Log("SteamController: scanning for a controller…");
    [_central scanForPeripheralsWithServices:nil options:nil];
}

- (void)connectTo:(CBPeripheral *)peripheral {
    [_central stopScan];
    _peripheral = peripheral;  // CoreBluetooth does not retain discovered peripherals
    _peripheral.delegate = self;
    [_central connectPeripheral:_peripheral options:nil];
}

// MARK: - Virtual gamepad

- (void)attachVirtualGamepad {
    if (_virtualJoystick) {
        return;
    }

    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.vendor_id = 0x28DE;
    desc.product_id = 0x1303;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1;  // the 15 standard buttons
    desc.axis_mask = (1u << desc.naxes) - 1u;
    desc.button_mask = (1u << desc.nbuttons) - 1u;
    desc.name = "Steam Controller";
    desc.userdata = (__bridge void *)self;
    desc.Rumble = steamControllerRumble;

    _virtualJoystickID = SDL_AttachVirtualJoystick(&desc);
    if (_virtualJoystickID == 0) {
        SDL_Log("SteamController: SDL_AttachVirtualJoystick failed: %s", SDL_GetError());
        return;
    }
    _virtualJoystick = SDL_OpenJoystick(_virtualJoystickID);
    SDL_Log("SteamController: virtual gamepad attached");
}

- (void)detachVirtualGamepad {
    if (_virtualJoystick) {
        SDL_CloseJoystick(_virtualJoystick);
        _virtualJoystick = nil;
    }
    if (_virtualJoystickID != 0) {
        SDL_DetachVirtualJoystick(_virtualJoystickID);
        _virtualJoystickID = 0;
    }
}

// Decodes one 45-byte input report and pushes it into the virtual gamepad.
- (void)applyReport:(const uint8_t *)data length:(NSUInteger)length {
    if (length < kReportLength || _virtualJoystick == nil) {
        return;
    }

    const uint32_t buttons = static_cast<uint32_t>(data[1]) |
                             (static_cast<uint32_t>(data[2]) << 8) |
                             (static_cast<uint32_t>(data[3]) << 16) |
                             (static_cast<uint32_t>(data[4]) << 24);
    for (const ButtonMapping &mapping : kButtonMappings) {
        const bool pressed = ((buttons >> mapping.sourceBit) & 1u) != 0u;
        SDL_SetJoystickVirtualButton(_virtualJoystick, mapping.sdlButton, pressed);
    }

    SDL_SetJoystickVirtualAxis(_virtualJoystick, SDL_GAMEPAD_AXIS_LEFTX, readInt16(data, 9));
    SDL_SetJoystickVirtualAxis(_virtualJoystick, SDL_GAMEPAD_AXIS_LEFTY, stickAxis(readInt16(data, 11)));
    SDL_SetJoystickVirtualAxis(_virtualJoystick, SDL_GAMEPAD_AXIS_RIGHTX, readInt16(data, 13));
    SDL_SetJoystickVirtualAxis(_virtualJoystick, SDL_GAMEPAD_AXIS_RIGHTY, stickAxis(readInt16(data, 15)));
    SDL_SetJoystickVirtualAxis(_virtualJoystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, triggerAxis(readInt16(data, 5)));
    SDL_SetJoystickVirtualAxis(_virtualJoystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, triggerAxis(readInt16(data, 7)));
}

// MARK: - Rumble

// Called from the game thread; the actual Bluetooth write is serialized onto
// the controller's queue.
- (void)writeRumbleLow:(uint16_t)lowFrequency high:(uint16_t)highFrequency {
    dispatch_async(_queue, ^{
        if (self->_peripheral == nil || self->_rumbleCharacteristic == nil) {
            return;
        }
        // The controller's strength fields are signed and peak at 0x7FFF, so
        // scale SDL's 0...65535 motor values into that range.
        const uint16_t left = lowFrequency >> 1;
        const uint16_t right = highFrequency >> 1;
        const uint16_t intensity = 0x7FFF;
        const uint8_t gain = 127;
        const uint8_t payload[9] = {
            0,
            static_cast<uint8_t>(intensity & 0xFF), static_cast<uint8_t>(intensity >> 8),
            static_cast<uint8_t>(left & 0xFF),  static_cast<uint8_t>(left >> 8),  gain,
            static_cast<uint8_t>(right & 0xFF), static_cast<uint8_t>(right >> 8), gain,
        };
        [self->_peripheral writeValue:[NSData dataWithBytes:payload length:sizeof(payload)]
                    forCharacteristic:self->_rumbleCharacteristic
                                 type:CBCharacteristicWriteWithResponse];
    });
}

// MARK: - CBCentralManagerDelegate

- (void)centralManagerDidUpdateState:(CBCentralManager *)central {
    if (central.state == CBManagerStatePoweredOn) {
        [self beginSearch];
    }
}

- (void)centralManager:(CBCentralManager *)central
 didDiscoverPeripheral:(CBPeripheral *)peripheral
     advertisementData:(NSDictionary<NSString *, id> *)advertisementData
                  RSSI:(NSNumber *)RSSI {
    if (_peripheral != nil) {
        return;
    }
    NSString *name = advertisementData[CBAdvertisementDataLocalNameKey];
    if (name == nil) {
        name = peripheral.name;
    }
    if ([name hasPrefix:@"Steam"]) {
        SDL_Log("SteamController: discovered '%s'", name.UTF8String);
        [self connectTo:peripheral];
    }
}

- (void)centralManager:(CBCentralManager *)central didConnectPeripheral:(CBPeripheral *)peripheral {
    [peripheral discoverServices:@[ [CBUUID UUIDWithString:kControllerService] ]];
}

- (void)centralManager:(CBCentralManager *)central
didDisconnectPeripheral:(CBPeripheral *)peripheral
                 error:(NSError *)error {
    SDL_Log("SteamController: disconnected");
    [self detachVirtualGamepad];
    _peripheral = nil;
    _inputCharacteristic = nil;
    _rumbleCharacteristic = nil;
    [self beginSearch];
}

// MARK: - CBPeripheralDelegate

- (void)peripheral:(CBPeripheral *)peripheral didDiscoverServices:(NSError *)error {
    for (CBService *service in peripheral.services) {
        if ([service.UUID isEqual:[CBUUID UUIDWithString:kControllerService]]) {
            [peripheral discoverCharacteristics:nil forService:service];
            return;
        }
    }
    SDL_Log("SteamController: controller service not found");
}

- (void)peripheral:(CBPeripheral *)peripheral
didDiscoverCharacteristicsForService:(CBService *)service
             error:(NSError *)error {
    for (CBCharacteristic *characteristic in service.characteristics) {
        if ([characteristic.UUID isEqual:[CBUUID UUIDWithString:kInputCharacteristicUUID]]) {
            _inputCharacteristic = characteristic;
        } else if ([characteristic.UUID isEqual:[CBUUID UUIDWithString:kRumbleCharacteristicUUID]]) {
            _rumbleCharacteristic = characteristic;
        }
    }
    if (_inputCharacteristic) {
        [peripheral setNotifyValue:YES forCharacteristic:_inputCharacteristic];
    } else {
        SDL_Log("SteamController: input characteristic not found");
    }
}

- (void)peripheral:(CBPeripheral *)peripheral
didUpdateValueForCharacteristic:(CBCharacteristic *)characteristic
             error:(NSError *)error {
    if (error != nil || ![characteristic.UUID isEqual:[CBUUID UUIDWithString:kInputCharacteristicUUID]]) {
        return;
    }
    NSData *value = characteristic.value;
    if (value.length < kReportLength) {
        return;
    }
    // The first report confirms the controller is live; attach now so SDL only
    // ever sees a gamepad that is actually sending input.
    [self attachVirtualGamepad];
    [self applyReport:static_cast<const uint8_t *>(value.bytes) length:value.length];
}

@end

// MARK: - C interface

static DuskSteamControllerBridge *gBridge = nil;

static bool steamControllerRumble(void *userdata, Uint16 lowFrequency, Uint16 highFrequency) {
    DuskSteamControllerBridge *bridge = (__bridge DuskSteamControllerBridge *)userdata;
    [bridge writeRumbleLow:lowFrequency high:highFrequency];
    return true;
}

void DuskSteamControllerStart(void) {
    if (gBridge != nil) {
        return;
    }
    gBridge = [[DuskSteamControllerBridge alloc] init];
    [gBridge start];
}

void DuskSteamControllerStop(void) {
    [gBridge stop];
    gBridge = nil;
}
