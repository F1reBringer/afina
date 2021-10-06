#include "StripedLRU.h"

namespace Afina {
namespace Backend {

bool StripedLRU::Put(const std::string &key, const std::string &value) {
    return MyStripes[MyStripesHash(key) % CountOfStripes]->Put(key, value);
}

bool StripedLRU::PutIfAbsent(const std::string &key, const std::string &value) {
    return MyStripes[MyStripesHash(key) % CountOfStripes]->PutIfAbsent(key, value);
}

bool StripedLRU::Set(const std::string &key, const std::string &value) {
    return MyStripes[MyStripesHash(key) % CountOfStripes]->Set(key, value);
}

bool StripedLRU::Delete(const std::string &key) {
    return MyStripes[MyStripesHash(key) % CountOfStripes]->Delete(key);
}

bool StripedLRU::Get(const std::string &key, std::string &value) {
    return MyStripes[MyStripesHash(key) % CountOfStripes]->Get(key, value);
}

} // namespace Backend
} // namespace Afina
