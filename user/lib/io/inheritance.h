#ifndef HUND_IO_INHERITANCE_H
#define HUND_IO_INHERITANCE_H

#include "protocol.h"
#include "types/fixed_array.h"

#define IO_RESOURCE_OWNER_LIMIT 64U
#define IO_INHERITANCE_GRANT_LIMIT 64U

typedef struct IOResourceOwner
{
    QueueConnectionID session;
    uint64_t pid;
} IOResourceOwner;

typedef IOResourceOwner * IOResourceOwnerPtr;
typedef const IOResourceOwner * ConstIOResourceOwnerPtr;
OptionalDef(IOResourceOwner);
bool IOResourceOwner__equals(void * first, void * second);
FixedArrayDefine(IOResourceOwner, IO_RESOURCE_OWNER_LIMIT, IOResourceOwners, IOResourceOwner__equals);
IOResourceOwner IOResourceOwner__new(QueueConnectionID session, uint64_t pid);
typedef IOResourceOwners * IOResourceOwnersPtr;
bool IOResourceOwners__contains(IOResourceOwnersPtr self, QueueConnectionID session, uint64_t pid);
bool IOResourceOwners__contains_pid(IOResourceOwnersPtr self, uint64_t pid);
IOStatus IOResourceOwners__add(IOResourceOwnersPtr self, QueueConnectionID session, uint64_t pid);
IOStatus IOResourceOwners__remove_owner(IOResourceOwnersPtr self, QueueConnectionID session, uint64_t pid);

typedef struct IOInheritanceGrant
{
    uint64_t token;
    IOHandle handle;
    Flags flags;
    IOResourceOwner source;
    uint64_t target_pid;
} IOInheritanceGrant;

typedef IOInheritanceGrant * IOInheritanceGrantPtr;
typedef const IOInheritanceGrant * ConstIOInheritanceGrantPtr;
OptionalDef(IOInheritanceGrant);
OptionalDef(IOInheritanceGrantPtr);
bool IOInheritanceGrant__equals(void * first, void * second);
FixedArrayDefine(IOInheritanceGrant, IO_INHERITANCE_GRANT_LIMIT, IOInheritanceGrants, IOInheritanceGrant__equals);

typedef struct IOInheritanceRegistry
{
    IOInheritanceGrants grants;
    uint64_t next_token;
} IOInheritanceRegistry;

typedef IOInheritanceRegistry * IOInheritanceRegistryPtr;
IOInheritanceRegistry IOInheritanceRegistry__new(void);
IOReply IOInheritanceRegistry__prepare(IOInheritanceRegistryPtr self, IOHandle handle, Flags flags,
                                       QueueConnectionID session, uint64_t pid);
IOStatus IOInheritanceRegistry__commit(IOInheritanceRegistryPtr self, IORequestPtr request);
Optional(IOInheritanceGrantPtr) IOInheritanceRegistry__claim(IOInheritanceRegistryPtr self, IORequestPtr request);
IOStatus IOInheritanceRegistry__abort(IOInheritanceRegistryPtr self, IORequestPtr request);
void IOInheritanceRegistry__complete(IOInheritanceRegistryPtr self, IOInheritanceGrantPtr grant);
bool IOInheritanceRegistry__retains_handle(IOInheritanceRegistryPtr self, IOHandle handle);
void IOInheritanceRegistry__release(IOInheritanceRegistryPtr self, QueueConnectionID session, uint64_t pid);

#endif
