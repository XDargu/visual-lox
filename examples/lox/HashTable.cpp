#include "HashTable.h"

#include "Object.h"
#include "Vm.h"

#define TABLE_MAX_LOAD 0.75

size_t Hasher::operator()(ObjString* key) const
{
    return key->hash;
}

bool TableCpp::set(ObjString* key, const Value& value)
{
    auto result = entries.insert({ key->hash, Entry({key, value}) });
    if (!result.second)
    {
        result.first->second.value = value;
        return false;
    }
    return true;
}

bool TableCpp::get(ObjString* key, Value* value)
{
    auto result = entries.find(key->hash);
    if (result == entries.end()) return false;
    *value = result->second.value;
    return true;
}

bool TableCpp::remove(ObjString* key)
{
    const size_t erasedElems = entries.erase(key->hash);
    return erasedElems > 0;
}

ObjString* TableCpp::findString(const char* chars, int length, uint32_t hash)
{
    auto result = entries.find(hash);
    return result == entries.end() ? nullptr : result->second.key;
}

void TableCpp::mark()
{
    VM& vm = VM::getInstance();
    for (EntriesMap::value_type& pair : entries)
    {
        Entry& entry = pair.second;
        vm.markObject(entry.key);
        vm.markValue(entry.value);
    }
}

void TableCpp::removeWhite()
{
    EntriesMap::iterator it = entries.begin();
    while (it != entries.end())
    {
        Entry& entry = it->second;
        if (entry.key != nullptr && !entry.key->isMarked)
        {
            it = entries.erase(it);
        }
        else {
            ++it;
        }
    }
}

Entry* findEntry(std::vector<Entry>& entries, size_t capacity, const ObjString* key)
{
    uint32_t index = key->hash % capacity;
    Entry* tombstone = nullptr;

    for (;;)
    {
        Entry* entry = &entries[index];
        if (entry->key == nullptr)
        {
            if (isNil(entry->value))
            {
                // Empty entry.
                return tombstone != nullptr ? tombstone : entry;
            }
            else
            {
                // We found a tombstone.
                if (tombstone == nullptr) tombstone = entry;
            }
        }
        else if (entry->key == key)
        {
            // We found the key.
            return entry;
        }

        index = (index + 1) % capacity;
    }

    return nullptr;
}

TableLox::TableLox()
    : count(0)
    , capacity(0)
    , entries()
{}

bool TableLox::set(ObjString* key, const Value& value)
{
    if (count + 1 > capacity * TABLE_MAX_LOAD)
    {
        const size_t nextCapacity = capacity < 2 ? 2 : capacity * 2;
        adjustCapacity(nextCapacity);
    }

    Entry* entry = findEntry(entries, capacity, key);
    const bool isNewKey = entry->key == nullptr;
    if (isNewKey && isNil(entry->value)) count++;

    entry->key = key;
    entry->value = value;
    return isNewKey;
}

void TableLox::adjustCapacity(size_t nextCapacity)
{
    // Instead of resizing the vector, we make a new one and swap with the old one
    // The vector would have to reallocate with the resizing anyway, and that way it's
    // easier to move the old values to the new hashtable
    std::vector<Entry> nextEntries(nextCapacity);

    count = 0;
    for (uint32_t i = 0; i < capacity; i++)
    {
        Entry* entry = &entries[i];
        if (entry->key == nullptr) continue;

        Entry* dest = findEntry(nextEntries, nextCapacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        count++;
    }

    std::swap(entries, nextEntries);

    capacity = nextCapacity;
}

bool TableLox::get(ObjString* key, Value* value)
{
    if (count == 0) return false;

    Entry* entry = findEntry(entries, capacity, key);
    if (entry->key == nullptr) return false;

    *value = entry->value;
    return true;
}

bool TableLox::remove(ObjString* key)
{
    if (count == 0) return false;

    // Find the entry.
    Entry* entry = findEntry(entries, capacity, key);
    if (entry->key == nullptr) return false;

    // Place a tombstone in the entry.
    entry->key = nullptr;
    entry->value = Value(true);
    return true;
}

ObjString* TableLox::findString(const char* chars, int length, uint32_t hash)
{
    if (count == 0) return nullptr;

    uint32_t index = hash % capacity;
    for (;;)
    {
        Entry* entry = &entries[index];
        if (entry->key == NULL)
        {
            // Stop if we find an empty non-tombstone entry.
            if (isNil(entry->value)) return NULL;
        }
        else if (entry->key->length == length &&
            entry->key->hash == hash &&
            memcmp(&entry->key->chars[0], chars, length) == 0)
        {
            // We found it.
            return entry->key;
        }

        index = (index + 1) % capacity;
    }
}

void TableLox::mark()
{
    VM& vm = VM::getInstance();
    for (int i = 0; i < capacity; i++)
    {
        Entry& entry = entries[i];
        vm.markObject(entry.key);
        vm.markValue(entry.value);
    }
}

void TableLox::removeWhite()
{
    for (int i = 0; i < capacity; i++)
    {
        Entry* entry = &entries[i];
        if (entry->key != NULL && !entry->key->isMarked)
        {
            remove(entry->key);
        }
    }
}

void TableLox::copyTo(TableLox& to) const
{
    for (uint32_t i = 0; i < capacity; i++)
    {
        const Entry* entry = &entries[i];
        if (entry->key != nullptr)
        {
            to.set(entry->key, entry->value);
        }
    }
}


void TableLox::Clear()
{
    entries.clear();
    count = 0;
    capacity = 0;
}
