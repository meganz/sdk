/**
 * @file filesystem.cpp
 * @brief Generic host filesystem access interfaces
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#include "mega/mega_utf8proc.h"
#include "mega/filesystem.h"
#include "mega/node.h"
#include "mega/megaclient.h"
#include "mega/logging.h"

namespace mega {
void FileSystemAccess::captimestamp(m_time_t* t)
{
    // FIXME: remove upper bound before the year 2100 and upgrade server-side timestamps to BIGINT
    if (*t > (uint32_t)-1) *t = (uint32_t)-1;
    else if (*t < 0) *t = 0;
}

bool FileSystemAccess::islchex(char c) const
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

// is c allowed in local fs names?
bool FileSystemAccess::islocalfscompatible(unsigned char c) const
{
    return c >= ' ' && !strchr("\\/:?\"<>|*", c);
}

// replace characters that are not allowed in local fs names with a %xx escape sequence
void FileSystemAccess::escapefsincompatible(string* name) const
{
    char buf[4];
    unsigned char c;

    // replace all occurrences of a badchar with %xx
    for (int i = name->size(); i--; )
    {
        c = (unsigned char)(*name)[i];

        if (!islocalfscompatible(c))
        {
            sprintf(buf, "%%%02x", c);
            name->replace(i, 1, buf);
        }
    }
}

void FileSystemAccess::unescapefsincompatible(string* name) const
{
    for (int i = name->size() - 2; i-- > 0; )
    {
        // conditions for unescaping: %xx must be well-formed and encode an incompatible character
        if ((*name)[i] == '%' && islchex((*name)[i + 1]) && islchex((*name)[i + 2]))
        {
            char c = (MegaClient::hexval((*name)[i + 1]) << 4) + MegaClient::hexval((*name)[i + 2]);

            if (!islocalfscompatible((unsigned char)c))
            {
                name->replace(i, 3, &c, 1);
            }
        }
    }
}

// escape forbidden characters, then convert to local encoding
void FileSystemAccess::name2local(string* filename) const
{
    escapefsincompatible(filename);

    string t = *filename;

    path2local(&t, filename);
}

void FileSystemAccess::normalize(string* filename) const
{
    if (!filename) return;

    const char* cfilename = filename->c_str();
    size_t fnsize = filename->size();
    string result;

    for (size_t i = 0; i < fnsize; )
    {
        // allow NUL bytes between valid UTF-8 sequences
        if (!cfilename[i])
        {
            result.append("", 1);
            i++;
            continue;
        }

        const char* substring = cfilename + i;
        char* normalized = (char*)utf8proc_NFC((uint8_t*)substring);

        if (!normalized)
        {
            filename->clear();
            return;
        }

        result.append(normalized);
        free(normalized);

        i += strlen(substring);
    }

    *filename = result;
}

// convert from local encoding, then unescape escaped forbidden characters
void FileSystemAccess::local2name(string* filename) const
{
    string t = *filename;

    local2path(&t, filename);

    unescapefsincompatible(filename);
}

// default DirNotify: no notification available
DirNotify::DirNotify(string* clocalbasepath, string* cignore)
{
    localbasepath = *clocalbasepath;
    ignore = *cignore;

    failed = true;
    error = false;
}

// notify base LocalNode + relative path/filename
void DirNotify::notify(notifyqueue q, LocalNode* l, const char* localpath, size_t len, bool immediate)
{
    notifyq[q].resize(notifyq[q].size() + 1);
    notifyq[q].back().timestamp = immediate ? 0 : Waiter::ds;
    notifyq[q].back().localnode = l;
    notifyq[q].back().path.assign(localpath, len);
}

// default: no fingerprint
fsfp_t DirNotify::fsfingerprint()
{
    return 0;
}

DirNotify* FileSystemAccess::newdirnotify(string* localpath, string* ignore)
{
    return new DirNotify(localpath, ignore);
}

FileAccess::FileAccess(Waiter *waiter)
{
    this->waiter = waiter;
}

// open file for reading
bool FileAccess::fopen(string* name)
{
    localname.resize(1);
    updatelocalname(name);

    return sysstat(&mtime, &size);
}

bool FileAccess::isfolder(string *name)
{
    fopen(name);
    return (type == FOLDERNODE);
}

// check if size and mtime are unchanged, then open for reading
bool FileAccess::openf()
{
    if (!localname.size())
    {
        return true;
    }

    m_time_t curr_mtime;
    m_off_t curr_size;

    if (!sysstat(&curr_mtime, &curr_size) || curr_mtime != mtime || curr_size != size)
    {
        return false;
    }

    return sysopen();
}

void FileAccess::closef()
{
    if (localname.size())
    {
        sysclose();
    }
}

void FileAccess::asyncopfinished(void *param)
{
    AsyncIOContext *context = (AsyncIOContext *)param;
    if (context->waiter)
    {
        context->waiter->notify();
    }
}

AsyncIOContext *FileAccess::asyncfread(string *dst, unsigned len, unsigned pad, m_off_t pos)
{
    LOG_verbose << "Async read start";
    dst->resize(len + pad);

    AsyncIOContext *context = newasynccontext();
    context->op = AsyncIOContext::READ;
    context->pos = pos;
    context->len = len;
    context->pad = pad;
    context->buffer = (byte *)dst->data();
    context->waiter = waiter;
    context->userCallback = asyncopfinished;
    context->userData = context;

    if (!openf())
    {
        LOG_warn << "Async read failed opening file";
        context->failed = true;
        context->retry = false;
        context->finished = true;
        asyncopfinished(context);
        return context;
    }

    asyncsysread(context);
    return context;
}

void FileAccess::asyncsysread(AsyncIOContext *context)
{
    context->failed = true;
    context->retry = false;
    context->finished = true;
    if (context->userCallback)
    {
        context->userCallback(context->userData);
    }
}

AsyncIOContext *FileAccess::asyncfwrite(const byte* data, unsigned len, m_off_t pos)
{
    LOG_verbose << "Async write start";

    AsyncIOContext *context = newasynccontext();
    context->op = AsyncIOContext::WRITE;
    context->pos = pos;
    context->len = len;
    context->buffer = (byte *)data;
    context->waiter = waiter;
    context->userCallback = asyncopfinished;
    context->userData = context;

    asyncsyswrite(context);
    return context;
}

void FileAccess::asyncsyswrite(AsyncIOContext *context)
{
    context->failed = true;
    context->retry = false;
    context->finished = true;
    if (context->userCallback)
    {
        context->userCallback(context->userData);
    }
}

AsyncIOContext *FileAccess::newasynccontext()
{
    return new AsyncIOContext();
}

bool FileAccess::fread(string* dst, unsigned len, unsigned pad, m_off_t pos)
{
    if (!openf())
    {
        return false;
    }

    bool r;

    dst->resize(len + pad);

    if ((r = sysread((byte*)dst->data(), len, pos)))
    {
        memset((char*)dst->data() + len, 0, pad);
    }

    closef();

    return r;
}

bool FileAccess::frawread(byte* dst, unsigned len, m_off_t pos)
{
    if (!openf())
    {
        return false;
    }

    bool r = sysread(dst, len, pos);

    closef();

    return r;
}

AsyncIOContext::AsyncIOContext()
{
    op = NONE;
    pos = 0;
    len = 0;
    pad = 0;
    buffer = NULL;
    waiter = NULL;

    userCallback = NULL;
    userData = NULL;
    finished = false;
    failed = false;
    retry = false;
}

} // namespace
