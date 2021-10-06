#ifndef AFINA_STORAGE_STRIPED_LRU_H
#define AFINA_STORAGE_STRIPED_LRU_H

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <afina/Storage.h>
#include "ThreadSafeSimpleLRU.h"

namespace Afina {
namespace Backend {


constexpr std::size_t MinStripeSize = 1u * 1024 * 1024;


class StripedLRU : public Afina::Storage
{
private:
    StripedLRU(std::size_t MaxStripes, std::size_t StripesCountArg)
        : CountOfStripes(StripesCountArg)
        {
            for (size_t i = 0; i < StripesCountArg; i++)
            {
                MyStripes.push_back(std::unique_ptr<ThreadSafeSimplLRU> (new ThreadSafeSimplLRU(MaxStripes)));
            }
        }

public:
    ~StripedLRU() {}

    static std::unique_ptr<StripedLRU>
    BuildStripedLRU(std::size_t MaxMemory = 1024, std::size_t StripesCountArg = 8)
    {
        std::size_t MaxStripes = MaxMemory / StripesCountArg;

        if (MaxStripes < MinStripeSize) 
        {
            throw std::runtime_error("Stripe size < MIN_STRIPE_SIZE");
        }

        return std::move(std::unique_ptr<StripedLRU>(new StripedLRU(MaxStripes, StripesCountArg)));
    }

    bool Put(const std::string &key, const std::string &value) override;

    bool PutIfAbsent(const std::string &key, const std::string &value) override;

    bool Set(const std::string &key, const std::string &value) override;

    bool Delete(const std::string &key) override;

    bool Get(const std::string &key, std::string &value) override;

private:
    std::vector<std::unique_ptr<ThreadSafeSimplLRU>> MyStripes;
    std::hash <std::string> MyStripesHash;
    std::size_t CountOfStripes;
};

} // namespace Backend
} // namespace Afina

#endif // AFINA_STORAGE_STRIPED_LRU_H
