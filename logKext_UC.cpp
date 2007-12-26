/*
 *  logKext_UC.cpp
 *  logKext
 *
 *  Created by Braden Thomas on 5/9/05.
 *
 */

#include "logKext.h"

#define super IOUserClient
OSDefineMetaClassAndStructors( logKextUserClient, IOUserClient );

bool logKextUserClient::initWithTask( task_t owningTask,
                                        void * securityID,
                                        UInt32 type,
                                        OSDictionary * properties )
{
	if (clientHasPrivilege(owningTask, kIOClientPrivilegeAdministrator)!=kIOReturnSuccess)
	{
		IOLog( "logKextUserClient::Error: unprivileged task attempted to init\n");	
		return false;
	}

	#ifdef DEBUG
		IOLog("logKextUserClient::initWithTask(type %ld)\n", type);
	#endif
	
	if (!super::initWithTask(owningTask, securityID, type))
        return false;

    if (!owningTask)
		return false;
	
    fTask = owningTask;	// remember who instantiated us
	fProvider = NULL;
	
    return true;
}


bool logKextUserClient::start( IOService* provider )
{    
	#ifdef DEBUG
		IOLog( "logKextUserClient::start\n" );
	#endif
    
    if( !super::start( provider ) )
        return false;
    
    // see if it's the correct class and remember it at the same time
    fProvider = OSDynamicCast( com_fsb_iokit_logKext, provider );
    if( !fProvider )
        return false;
		
	fProvider->activate();	// call activate on kext to hook keyboards

	return true;
}

void logKextUserClient::stop( IOService* provider )
{
	#ifdef DEBUG
		IOLog( "logKextUserClient::stop\n" );
	#endif

    super::stop( provider );
}


IOReturn logKextUserClient::clientClose( void )
{
	#ifdef DEBUG
		IOLog( "logKextUserClient::clientClose\n" );
	#endif
    
	fProvider->deactivate();	// call deactivate on kext to unhook keyboards
	
    fTask = NULL;
    fProvider = NULL;
    terminate();
    
    return kIOReturnSuccess;

}

IOExternalMethod* logKextUserClient::getTargetAndMethodForIndex(IOService** targetP, UInt32 index )
{
	*targetP = fProvider;	// driver is target of all our external methods
    
    // validate index and return the appropriate IOExternalMethod
    if( index < kNumlogKextMethods )
        return (IOExternalMethod*) &externalMethods[index];
    else
        return NULL;
}
