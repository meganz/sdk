/*
 * userAttributes.cpp
 *
 *  Created on: 26/02/2015
 *      Author: michael
 */

#include "userAttributes.h"
#include <stdexcept>
#include "sharedbuffer.h"
#include "megaapi.h"

namespace mega
{

void
UserAttributes::addUserAttribute(std::string &valueName, ValueMap &value,
        Visibility visibility)
{
    if(tlvStore.find(valueName) != tlvStore.end())
    {
        throw std::runtime_error(ATTRIBUTE_EXISTS);
    }

    SharedBuffer buffer = this->valueMapToTlv(value, visibility);
    tlvStore.insert({ valueName, buffer });
}

ValueMap
UserAttributes::getUserAttribute(std::string &valueName)
{

    auto i = tlvStore.find(valueName);
    if(i == tlvStore.end())
    {
        throw std::runtime_error(VALUE_NOT_FOUND);
    }

    ValueMap retVal = this->tlvToValueMap(i->second);

    return retVal;
}

SharedBuffer
UserAttributes::getUserAttributeTlv(std::string &valueName)
{

    auto i = tlvStore.find(valueName);
    if(i == tlvStore.end())
    {
        throw std::runtime_error(VALUE_NOT_FOUND);
    }

    return i->second;
}

SharedBuffer
UserAttributes::valueMapToTlv(ValueMap &valueMap, Visibility visibility)
{
    int length = 0;
    for(auto &i : *valueMap)
    {
        length += (i.first.length() + i.second.size + 1 + 2);
    }

    SharedBuffer buffer(length, visibility);
    int offset = 0;
    for(auto &i : *valueMap)
    {
        addValue(i.first, i.second, buffer, &offset);
    }

    return buffer;
}

void
UserAttributes::addValue(const std::string &valueName, SharedBuffer &value, SharedBuffer &buffer,
        int *offset)
{
    memcpy(buffer.get() + *offset, valueName.c_str(), valueName.length());
    *offset += valueName.length();
    buffer.get()[*offset] = '\0';
    buffer.get()[++*offset] = (unsigned char)(value.size >> 8);
    buffer.get()[++*offset] = (value.size);
    memcpy(buffer.get() + ++*offset, value.get(), value.size);
    *offset += value.size;
}


ValueMap
UserAttributes::tlvToValueMap(SharedBuffer &data)
{
    ValueMap map(new std::map<std::string, SharedBuffer>());

    int oldVal = 0;
    for(unsigned int x = 0; x < data.size; x++)
    {
        while(data.get()[x] != '\0' && x < data.size && ++x);
        if(x == data.size)
        {
            throw std::runtime_error(NULL_DELIMITER_NOT_FOUND);
        }

        std::string tag((char*)data.get() + oldVal, x - oldVal);

        if(x + 2 >= data.size)
        {
            throw std::runtime_error(INVALID_DATA_LENGTH);
        }

        short size = data.get()[++x];
        size <<= 8;
        size = (data.get()[++x] | size);

        if((x + size) >= data.size)
        {
            throw std::runtime_error(INVALID_DATA_LENGTH);
        }

        SharedBuffer value(size);
        memcpy(value.get(), data.get() + ++x, value.size);
        map->insert({tag, value});
        x += size;
        oldVal = x;

    }

    return map;
}

}

