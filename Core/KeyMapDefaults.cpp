#include "ppsspp_config.h"

#include "Common/CommonFuncs.h"
#include "Common/Input/KeyCodes.h"
#include "Common/Input/InputState.h"
#include "Common/System/System.h"
#include "Core/KeyMapDefaults.h"
#include "Core/KeyMap.h"

#include "Core/HLE/sceCtrl.h"

namespace KeyMap {

struct DefMappingStruct {
	int pspKey;
	int keyOrAxis;
	int direction;  // if 0, it's a key, otherwise an axis.
};

static const DefMappingStruct defaultQwertyKeyboardKeyMap[] = {
	{CTRL_SQUARE, NKCODE_A},
	{CTRL_TRIANGLE, NKCODE_S},
	{CTRL_CIRCLE, NKCODE_X},
	{CTRL_CROSS, NKCODE_Z},
	{CTRL_LTRIGGER, NKCODE_Q},
	{CTRL_RTRIGGER, NKCODE_W},

	{CTRL_START, NKCODE_SPACE},
#ifdef _WIN32
	{CTRL_SELECT, NKCODE_V},
#else
	{CTRL_SELECT, NKCODE_ENTER},
#endif
	{CTRL_UP   , NKCODE_DPAD_UP},
	{CTRL_DOWN , NKCODE_DPAD_DOWN},
	{CTRL_LEFT , NKCODE_DPAD_LEFT},
	{CTRL_RIGHT, NKCODE_DPAD_RIGHT},
	{VIRTKEY_AXIS_Y_MAX, NKCODE_I},
	{VIRTKEY_AXIS_Y_MIN, NKCODE_K},
	{VIRTKEY_AXIS_X_MIN, NKCODE_J},
	{VIRTKEY_AXIS_X_MAX, NKCODE_L},
	{VIRTKEY_RAPID_FIRE, NKCODE_SHIFT_LEFT},
	{VIRTKEY_FASTFORWARD, NKCODE_TAB},
	{VIRTKEY_SPEED_TOGGLE, NKCODE_GRAVE},
	{VIRTKEY_PAUSE       , NKCODE_ESCAPE},
	{VIRTKEY_REWIND      , NKCODE_DEL},
	{VIRTKEY_ANALOG_LIGHTLY, NKCODE_SHIFT_RIGHT},
};

static const DefMappingStruct defaultAzertyKeyboardKeyMap[] = {
	{CTRL_SQUARE, NKCODE_Q},
	{CTRL_TRIANGLE, NKCODE_S},
	{CTRL_CIRCLE, NKCODE_X},
	{CTRL_CROSS, NKCODE_W},
	{CTRL_LTRIGGER, NKCODE_A},
	{CTRL_RTRIGGER, NKCODE_Z},

	{CTRL_START, NKCODE_SPACE},
#ifdef _WIN32
	{CTRL_SELECT, NKCODE_V},
#else
	{CTRL_SELECT, NKCODE_ENTER},
#endif
	{CTRL_UP   , NKCODE_DPAD_UP},
	{CTRL_DOWN , NKCODE_DPAD_DOWN},
	{CTRL_LEFT , NKCODE_DPAD_LEFT},
	{CTRL_RIGHT, NKCODE_DPAD_RIGHT},
	{VIRTKEY_AXIS_Y_MAX, NKCODE_I},
	{VIRTKEY_AXIS_Y_MIN, NKCODE_K},
	{VIRTKEY_AXIS_X_MIN, NKCODE_J},
	{VIRTKEY_AXIS_X_MAX, NKCODE_L},
	{VIRTKEY_RAPID_FIRE, NKCODE_SHIFT_LEFT},
	{VIRTKEY_FASTFORWARD, NKCODE_TAB},
	{VIRTKEY_SPEED_TOGGLE, NKCODE_GRAVE},
	{VIRTKEY_PAUSE       , NKCODE_ESCAPE},
	{VIRTKEY_REWIND      , NKCODE_DEL},
	{VIRTKEY_ANALOG_LIGHTLY, NKCODE_SHIFT_RIGHT},
};

static const DefMappingStruct defaultQwertzKeyboardKeyMap[] = {
	{CTRL_SQUARE, NKCODE_A},
	{CTRL_TRIANGLE, NKCODE_S},
	{CTRL_CIRCLE, NKCODE_X},
	{CTRL_CROSS, NKCODE_Y},
	{CTRL_LTRIGGER, NKCODE_Q},
	{CTRL_RTRIGGER, NKCODE_W},

	{CTRL_START, NKCODE_SPACE},
#ifdef _WIN32
	{CTRL_SELECT, NKCODE_V},
#else
	{CTRL_SELECT, NKCODE_ENTER},
#endif
	{CTRL_UP   , NKCODE_DPAD_UP},
	{CTRL_DOWN , NKCODE_DPAD_DOWN},
	{CTRL_LEFT , NKCODE_DPAD_LEFT},
	{CTRL_RIGHT, NKCODE_DPAD_RIGHT},
	{VIRTKEY_AXIS_Y_MAX, NKCODE_I},
	{VIRTKEY_AXIS_Y_MIN, NKCODE_K},
	{VIRTKEY_AXIS_X_MIN, NKCODE_J},
	{VIRTKEY_AXIS_X_MAX, NKCODE_L},
	{VIRTKEY_RAPID_FIRE, NKCODE_SHIFT_LEFT},
	{VIRTKEY_FASTFORWARD, NKCODE_TAB},
	{VIRTKEY_SPEED_TOGGLE, NKCODE_GRAVE},
	{VIRTKEY_PAUSE       , NKCODE_ESCAPE},
	{VIRTKEY_REWIND      , NKCODE_DEL},
	{VIRTKEY_ANALOG_LIGHTLY, NKCODE_SHIFT_RIGHT},
};

static const DefMappingStruct defaultXInputKeyMap[] = {
	{VIRTKEY_AXIS_X_MIN, JOYSTICK_AXIS_X, -1},
	{VIRTKEY_AXIS_X_MAX, JOYSTICK_AXIS_X, +1},
	{VIRTKEY_AXIS_Y_MIN, JOYSTICK_AXIS_Y, -1},
	{VIRTKEY_AXIS_Y_MAX, JOYSTICK_AXIS_Y, +1},
	{CTRL_CROSS          , NKCODE_BUTTON_A},
	{CTRL_CIRCLE         , NKCODE_BUTTON_B},
	{CTRL_SQUARE         , NKCODE_BUTTON_X},
	{CTRL_TRIANGLE       , NKCODE_BUTTON_Y},
	{CTRL_UP             , NKCODE_DPAD_UP},
	{CTRL_RIGHT          , NKCODE_DPAD_RIGHT},
	{CTRL_DOWN           , NKCODE_DPAD_DOWN},
	{CTRL_LEFT           , NKCODE_DPAD_LEFT},
	{CTRL_START          , NKCODE_BUTTON_START},
	{CTRL_SELECT         , NKCODE_BUTTON_SELECT},
	{CTRL_LTRIGGER       , NKCODE_BUTTON_L1},
	{CTRL_RTRIGGER       , NKCODE_BUTTON_R1},
	{VIRTKEY_FASTFORWARD  , JOYSTICK_AXIS_RTRIGGER, +1},
	{VIRTKEY_SPEED_TOGGLE, NKCODE_BUTTON_THUMBR},
	{VIRTKEY_PAUSE       , JOYSTICK_AXIS_LTRIGGER, +1},
	{VIRTKEY_PAUSE,        NKCODE_HOME},
	{VIRTKEY_FASTFORWARD , JOYSTICK_AXIS_GAS, +1},
	{VIRTKEY_PAUSE       , JOYSTICK_AXIS_BRAKE, +1},
};

static const DefMappingStruct defaultShieldKeyMap[] = {
	{CTRL_CROSS, NKCODE_BUTTON_A},
	{CTRL_CIRCLE   ,NKCODE_BUTTON_B},
	{CTRL_SQUARE   ,NKCODE_BUTTON_X},
	{CTRL_TRIANGLE ,NKCODE_BUTTON_Y},
	{CTRL_START,  NKCODE_BUTTON_START},
	{CTRL_SELECT, JOYSTICK_AXIS_LTRIGGER, +1},
	{CTRL_LTRIGGER, NKCODE_BUTTON_L1},
	{CTRL_RTRIGGER, NKCODE_BUTTON_R1},
	{VIRTKEY_AXIS_X_MIN, JOYSTICK_AXIS_X, -1},
	{VIRTKEY_AXIS_X_MAX, JOYSTICK_AXIS_X, +1},
	{VIRTKEY_AXIS_Y_MIN, JOYSTICK_AXIS_Y, +1},
	{VIRTKEY_AXIS_Y_MAX, JOYSTICK_AXIS_Y, -1},
	{CTRL_LEFT, JOYSTICK_AXIS_HAT_X, -1},
	{CTRL_RIGHT, JOYSTICK_AXIS_HAT_X, +1},
	{CTRL_UP, JOYSTICK_AXIS_HAT_Y, -1},
	{CTRL_DOWN, JOYSTICK_AXIS_HAT_Y, +1},
	{VIRTKEY_FASTFORWARD, JOYSTICK_AXIS_RTRIGGER, +1 },
	{VIRTKEY_PAUSE, NKCODE_BACK },
};

static const DefMappingStruct defaultMOQI7SKeyMap[] = {
	{CTRL_CROSS, NKCODE_BUTTON_A},
	{CTRL_CIRCLE, NKCODE_BUTTON_B},
	{CTRL_SQUARE, NKCODE_BUTTON_X},
	{CTRL_TRIANGLE, NKCODE_BUTTON_Y},
	{CTRL_START,  JOYSTICK_AXIS_Z, +1},
	{CTRL_SELECT, JOYSTICK_AXIS_Z, -1},
	{CTRL_LTRIGGER, NKCODE_BUTTON_L1},
	{CTRL_RTRIGGER, NKCODE_BUTTON_R1},
	{VIRTKEY_AXIS_X_MIN, JOYSTICK_AXIS_X, -1},
	{VIRTKEY_AXIS_X_MAX, JOYSTICK_AXIS_X, +1},
	{VIRTKEY_AXIS_Y_MIN, JOYSTICK_AXIS_Y, +1},
	{VIRTKEY_AXIS_Y_MAX, JOYSTICK_AXIS_Y, -1},
	{CTRL_LEFT, JOYSTICK_AXIS_HAT_X, -1},
	{CTRL_RIGHT, JOYSTICK_AXIS_HAT_X, +1},
	{CTRL_UP, JOYSTICK_AXIS_HAT_Y, -1},
	{CTRL_DOWN, JOYSTICK_AXIS_HAT_Y, +1},
	{VIRTKEY_FASTFORWARD, JOYSTICK_AXIS_RZ, +1 },
	{VIRTKEY_PAUSE, NKCODE_BACK },
};

static const DefMappingStruct defaultAndroidXboxControllerMap[] = {
	{CTRL_CROSS          , NKCODE_BUTTON_A},
	{CTRL_CIRCLE         , NKCODE_BUTTON_B},
	{CTRL_SQUARE         , NKCODE_BUTTON_X},
	{CTRL_TRIANGLE       , NKCODE_BUTTON_Y},
	// The hat for DPAD is standard for bluetooth pads, which is the most likely pads on Android I think.
	{CTRL_LEFT           , NKCODE_DPAD_LEFT},
	{CTRL_RIGHT          , NKCODE_DPAD_RIGHT},
	{CTRL_UP             , NKCODE_DPAD_UP},
	{CTRL_DOWN           , NKCODE_DPAD_DOWN},
	{CTRL_START          , NKCODE_BUTTON_START},
	{CTRL_SELECT         , NKCODE_BACK},
	{CTRL_LTRIGGER       , NKCODE_BUTTON_L1},
	{CTRL_RTRIGGER       , NKCODE_BUTTON_R1},
	{VIRTKEY_FASTFORWARD , JOYSTICK_AXIS_RTRIGGER, +1},
	{VIRTKEY_PAUSE       , JOYSTICK_AXIS_LTRIGGER, +1},
	{VIRTKEY_FASTFORWARD , JOYSTICK_AXIS_GAS, +1},
	{VIRTKEY_PAUSE       , JOYSTICK_AXIS_BRAKE, +1},
	{VIRTKEY_AXIS_X_MIN, JOYSTICK_AXIS_X, -1},
	{VIRTKEY_AXIS_X_MAX, JOYSTICK_AXIS_X, +1},
	{VIRTKEY_AXIS_Y_MIN, JOYSTICK_AXIS_Y, +1},
	{VIRTKEY_AXIS_Y_MAX, JOYSTICK_AXIS_Y, -1},
};

// Retroid reports its controller as "Retro Station Controller".
// It's very similar to the Android Xbox mapping, just with main buttons swapped around.
static const DefMappingStruct defaultRetroStationControllerMap[] = {
	{CTRL_CROSS          , NKCODE_BUTTON_B},
	{CTRL_CIRCLE         , NKCODE_BUTTON_A},
	{CTRL_SQUARE         , NKCODE_BUTTON_Y},
	{CTRL_TRIANGLE       , NKCODE_BUTTON_X},
	// The hat for DPAD is standard for bluetooth pads, which is the most likely pads on Android I think.
	{CTRL_LEFT           , NKCODE_DPAD_LEFT},
	{CTRL_RIGHT          , NKCODE_DPAD_RIGHT},
	{CTRL_UP             , NKCODE_DPAD_UP},
	{CTRL_DOWN           , NKCODE_DPAD_DOWN},
	{CTRL_START          , NKCODE_BUTTON_START},
	{CTRL_SELECT         , NKCODE_BACK},
	{CTRL_LTRIGGER       , NKCODE_BUTTON_L1},
	{CTRL_RTRIGGER       , NKCODE_BUTTON_R1},
	{VIRTKEY_FASTFORWARD , JOYSTICK_AXIS_RTRIGGER, +1},
	{VIRTKEY_PAUSE       , JOYSTICK_AXIS_LTRIGGER, +1},
	{VIRTKEY_AXIS_X_MIN, JOYSTICK_AXIS_X, -1},
	{VIRTKEY_AXIS_X_MAX, JOYSTICK_AXIS_X, +1},
	{VIRTKEY_AXIS_Y_MIN, JOYSTICK_AXIS_Y, +1},
	{VIRTKEY_AXIS_Y_MAX, JOYSTICK_AXIS_Y, -1},
};


static const DefMappingStruct defaultPadMapAndroid[] = {
	{CTRL_CROSS          , NKCODE_BUTTON_A},
	{CTRL_CIRCLE         , NKCODE_BUTTON_B},
	{CTRL_SQUARE         , NKCODE_BUTTON_X},
	{CTRL_TRIANGLE       , NKCODE_BUTTON_Y},
	// The hat for DPAD is standard for bluetooth pads, which is the most likely pads on Android I think.
	{CTRL_LEFT           , JOYSTICK_AXIS_HAT_X, -1},
	{CTRL_LEFT           , NKCODE_DPAD_LEFT},
	{CTRL_RIGHT          , JOYSTICK_AXIS_HAT_X, +1},
	{CTRL_RIGHT          , NKCODE_DPAD_RIGHT},
	{CTRL_UP             , JOYSTICK_AXIS_HAT_Y, -1},
	{CTRL_UP             , NKCODE_DPAD_UP},
	{CTRL_DOWN           , JOYSTICK_AXIS_HAT_Y, +1},
	{CTRL_DOWN           , NKCODE_DPAD_DOWN},

	{CTRL_START          , NKCODE_BUTTON_START},
	{CTRL_SELECT         , NKCODE_BUTTON_SELECT},
	{CTRL_LTRIGGER       , NKCODE_BUTTON_L1},
	{CTRL_RTRIGGER       , NKCODE_BUTTON_R1},
	{VIRTKEY_FASTFORWARD , JOYSTICK_AXIS_RTRIGGER, +1},
	{VIRTKEY_FASTFORWARD , NKCODE_BUTTON_R2},
	{VIRTKEY_PAUSE       , JOYSTICK_AXIS_LTRIGGER, +1},
	{VIRTKEY_PAUSE       , NKCODE_BUTTON_L2 },
	{VIRTKEY_FASTFORWARD , JOYSTICK_AXIS_GAS, +1},
	{VIRTKEY_PAUSE       , JOYSTICK_AXIS_BRAKE, +1},
	{VIRTKEY_AXIS_X_MIN, JOYSTICK_AXIS_X, -1},
	{VIRTKEY_AXIS_X_MAX, JOYSTICK_AXIS_X, +1},
	{VIRTKEY_AXIS_Y_MIN, JOYSTICK_AXIS_Y, +1},
	{VIRTKEY_AXIS_Y_MAX, JOYSTICK_AXIS_Y, -1},
};

static const DefMappingStruct defaultPadMap[] = {
	{CTRL_CROSS          , NKCODE_BUTTON_2},
	{CTRL_CIRCLE         , NKCODE_BUTTON_3},
	{CTRL_SQUARE         , NKCODE_BUTTON_4},
	{CTRL_TRIANGLE       , NKCODE_BUTTON_1},
	{CTRL_UP             , NKCODE_DPAD_UP},
	{CTRL_RIGHT          , NKCODE_DPAD_RIGHT},
	{CTRL_DOWN           , NKCODE_DPAD_DOWN},
	{CTRL_LEFT           , NKCODE_DPAD_LEFT},
	{CTRL_START          , NKCODE_BUTTON_10},
	{CTRL_SELECT         , NKCODE_BUTTON_9},
	{CTRL_LTRIGGER       , NKCODE_BUTTON_7},
	{CTRL_RTRIGGER       , NKCODE_BUTTON_8},
	{VIRTKEY_AXIS_X_MIN, JOYSTICK_AXIS_X, -1},
	{VIRTKEY_AXIS_X_MAX, JOYSTICK_AXIS_X, +1},
	{VIRTKEY_AXIS_Y_MIN, JOYSTICK_AXIS_Y, +1},
	{VIRTKEY_AXIS_Y_MAX, JOYSTICK_AXIS_Y, -1},
	{VIRTKEY_PAUSE       , NKCODE_BUTTON_SELECT },
	{VIRTKEY_PAUSE       , NKCODE_BUTTON_L2 },
};

static const DefMappingStruct defaultOuyaMap[] = {
	{CTRL_CROSS          , NKCODE_BUTTON_A},
	{CTRL_CIRCLE         , NKCODE_BUTTON_B},
	{CTRL_SQUARE         , NKCODE_BUTTON_X},
	{CTRL_TRIANGLE       , NKCODE_BUTTON_Y},
	{CTRL_UP             , NKCODE_DPAD_UP},
	{CTRL_RIGHT          , NKCODE_DPAD_RIGHT},
	{CTRL_DOWN           , NKCODE_DPAD_DOWN},
	{CTRL_LEFT           , NKCODE_DPAD_LEFT},
	{CTRL_START          , NKCODE_BUTTON_R2},
	{CTRL_SELECT         , NKCODE_BUTTON_L2},
	{CTRL_LTRIGGER       , NKCODE_BUTTON_L1},
	{CTRL_RTRIGGER       , NKCODE_BUTTON_R1},
	{VIRTKEY_FASTFORWARD  , NKCODE_BUTTON_THUMBL},
	{VIRTKEY_PAUSE       , NKCODE_BUTTON_THUMBR},
	{VIRTKEY_AXIS_X_MIN, JOYSTICK_AXIS_X, -1},
	{VIRTKEY_AXIS_X_MAX, JOYSTICK_AXIS_X, +1},
	{VIRTKEY_AXIS_Y_MIN, JOYSTICK_AXIS_Y, +1},
	{VIRTKEY_AXIS_Y_MAX, JOYSTICK_AXIS_Y, -1},
};

static const DefMappingStruct defaultXperiaPlay[] = {
	{CTRL_CROSS          , NKCODE_BUTTON_CROSS},
	{CTRL_CIRCLE         , NKCODE_BUTTON_CIRCLE},
	{CTRL_SQUARE         , NKCODE_BUTTON_X},
	{CTRL_TRIANGLE       , NKCODE_BUTTON_Y},
	{CTRL_UP             , NKCODE_DPAD_UP},
	{CTRL_RIGHT          , NKCODE_DPAD_RIGHT},
	{CTRL_DOWN           , NKCODE_DPAD_DOWN},
	{CTRL_LEFT           , NKCODE_DPAD_LEFT},
	{CTRL_START          , NKCODE_BUTTON_START},
	{CTRL_SELECT         , NKCODE_BUTTON_SELECT},
	{CTRL_LTRIGGER       , NKCODE_BUTTON_L1},
	{CTRL_RTRIGGER       , NKCODE_BUTTON_R1},
	{VIRTKEY_AXIS_X_MIN, JOYSTICK_AXIS_X, -1},
	{VIRTKEY_AXIS_X_MAX, JOYSTICK_AXIS_X, +1},
	{VIRTKEY_AXIS_Y_MIN, JOYSTICK_AXIS_Y, -1},
	{VIRTKEY_AXIS_Y_MAX, JOYSTICK_AXIS_Y, +1},
};

static const DefMappingStruct defaultVRLeftController[] = {
	{CTRL_UP             , NKCODE_DPAD_UP},
	{CTRL_DOWN           , NKCODE_DPAD_DOWN},
	{CTRL_LEFT           , NKCODE_DPAD_LEFT},
	{CTRL_RIGHT          , NKCODE_DPAD_RIGHT},
	{CTRL_SELECT         , NKCODE_BUTTON_THUMBL},
	{CTRL_LTRIGGER       , NKCODE_BUTTON_X},
	{CTRL_RTRIGGER       , NKCODE_BUTTON_Y},
	{CTRL_SCREEN         , NKCODE_ALT_LEFT},
};

static const DefMappingStruct defaultVRRightController[] = {
	{CTRL_CIRCLE         , NKCODE_ALT_RIGHT},
	{CTRL_CROSS          , NKCODE_ENTER},
	{CTRL_SQUARE         , NKCODE_BUTTON_B},
	{CTRL_TRIANGLE       , NKCODE_BUTTON_A},
	{CTRL_START          , NKCODE_BUTTON_THUMBR},
	{VIRTKEY_AXIS_Y_MAX, NKCODE_DPAD_UP},
	{VIRTKEY_AXIS_Y_MIN, NKCODE_DPAD_DOWN},
	{VIRTKEY_AXIS_X_MIN, NKCODE_DPAD_LEFT},
	{VIRTKEY_AXIS_X_MAX, NKCODE_DPAD_RIGHT},
};

static void SetDefaultKeyMap(int deviceId, const DefMappingStruct *array, size_t count, bool replace) {
	for (size_t i = 0; i < count; i++) {
		if (array[i].direction == 0)
			SetInputMapping(array[i].pspKey, InputMapping(deviceId, array[i].keyOrAxis), replace);
		else
			SetInputMapping(array[i].pspKey, InputMapping(deviceId, array[i].keyOrAxis, array[i].direction), replace);
	}
	g_seenDeviceIds.insert(deviceId);
}

void SetDefaultKeyMap(DefaultMaps dmap, bool replace) {
	switch (dmap) {
	case DEFAULT_MAPPING_KEYBOARD:
	{
		int keyboardLayout = System_GetPropertyInt(SYSPROP_KEYBOARD_LAYOUT);
		switch (keyboardLayout) {
		case KEYBOARD_LAYOUT_QWERTZ:
			SetDefaultKeyMap(DEVICE_ID_KEYBOARD, defaultQwertzKeyboardKeyMap, ARRAY_SIZE(defaultQwertzKeyboardKeyMap), replace);
			break;
		case KEYBOARD_LAYOUT_AZERTY:
			SetDefaultKeyMap(DEVICE_ID_KEYBOARD, defaultAzertyKeyboardKeyMap, ARRAY_SIZE(defaultAzertyKeyboardKeyMap), replace);
			break;
		case KEYBOARD_LAYOUT_QWERTY:
		default:
			SetDefaultKeyMap(DEVICE_ID_KEYBOARD, defaultQwertyKeyboardKeyMap, ARRAY_SIZE(defaultQwertyKeyboardKeyMap), replace);
			break;
		}
	}
	break;
	case DEFAULT_MAPPING_XINPUT:
		SetDefaultKeyMap(DEVICE_ID_XINPUT_0, defaultXInputKeyMap, ARRAY_SIZE(defaultXInputKeyMap), replace);
		break;
	case DEFAULT_MAPPING_SHIELD:
		SetDefaultKeyMap(DEVICE_ID_PAD_0, defaultShieldKeyMap, ARRAY_SIZE(defaultShieldKeyMap), replace);
		break;
	case DEFAULT_MAPPING_MOQI_I7S:
		SetDefaultKeyMap(DEVICE_ID_PAD_0, defaultMOQI7SKeyMap, ARRAY_SIZE(defaultMOQI7SKeyMap), replace);
		break;
	case DEFAULT_MAPPING_PAD:
		SetDefaultKeyMap(DEVICE_ID_PAD_0, defaultPadMap, ARRAY_SIZE(defaultPadMap), replace);
		break;
	case DEFAULT_MAPPING_ANDROID_PAD:
		SetDefaultKeyMap(DEVICE_ID_PAD_0, defaultPadMapAndroid, ARRAY_SIZE(defaultPadMapAndroid), replace);
		break;
	case DEFAULT_MAPPING_OUYA:
		SetDefaultKeyMap(DEVICE_ID_PAD_0, defaultOuyaMap, ARRAY_SIZE(defaultOuyaMap), replace);
		break;
	case DEFAULT_MAPPING_XPERIA_PLAY:
		SetDefaultKeyMap(DEVICE_ID_DEFAULT, defaultXperiaPlay, ARRAY_SIZE(defaultXperiaPlay), replace);
		break;
	case DEFAULT_MAPPING_ANDROID_XBOX:
		SetDefaultKeyMap(DEVICE_ID_PAD_0, defaultAndroidXboxControllerMap, ARRAY_SIZE(defaultAndroidXboxControllerMap), replace);
		break;
	case DEFAULT_MAPPING_RETRO_STATION_CONTROLLER:
		SetDefaultKeyMap(DEVICE_ID_PAD_0, defaultRetroStationControllerMap, ARRAY_SIZE(defaultRetroStationControllerMap), replace);
		break;
	case DEFAULT_MAPPING_VR_HEADSET:
		SetDefaultKeyMap(DEVICE_ID_XR_CONTROLLER_LEFT, defaultVRLeftController, ARRAY_SIZE(defaultVRLeftController), replace);
		SetDefaultKeyMap(DEVICE_ID_XR_CONTROLLER_RIGHT, defaultVRRightController, ARRAY_SIZE(defaultVRRightController), replace);
		break;
	}

	UpdateNativeMenuKeys();
}

}  // namespace KeyMap
