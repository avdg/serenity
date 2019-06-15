#include "FullDevice.h"
#include <AK/StdLibExtras.h>
#include <AK/kstdio.h>
#include <LibC/errno_numbers.h>

FullDevice::FullDevice()
    : CharacterDevice(1, 7)
{
}

FullDevice::~FullDevice()
{
}

bool FullDevice::can_read(FileDescription&) const
{
    return true;
}

ssize_t FullDevice::read(FileDescription&, byte* buffer, ssize_t size)
{
    ssize_t count = min(PAGE_SIZE, size);
    memset(buffer, 0, (size_t)count);
    return count;
}

ssize_t FullDevice::write(FileDescription&, const byte*, ssize_t size)
{
    if (size == 0)
        return 0;
    return -ENOSPC;
}
