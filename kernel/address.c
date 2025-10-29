#include "address.h"

bool KernelAddress__equals(KernelAddressPtr self, KernelAddressPtr other)
{
    if (self == other)
    {
        return true;
    }
    if (self && other)
    {
        bool res = self->phys_addr == other->phys_addr;
        res &= self->virtual_addr == other->virtual_addr;
        res &= self->valid == other->valid;
        return res;
    }
    return false;
}


bool UserAddress__equals(UserAddressPtr self, UserAddressPtr other)
{
    if (self == other)
    {
        return true;
    }
    if (self && other)
    {
        bool res = self->virtual_addr == other->virtual_addr;
        res &= self->valid == other->valid;
        res &= self->execute == other->execute;
        res &= self->read == other->read;
        res &= self->write == other->write;
        res &= KernelAddress__equals(&self->kernel_addr, &other->kernel_addr);

        return res;
    }
    return false;
}