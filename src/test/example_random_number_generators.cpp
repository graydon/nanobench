#include <nanobench.h>

#include <fstream>
#include <iostream>
#include <random>
#include <thirdparty/doctest/doctest.h>

namespace {

// Benchmarks how fast we can get 64bit random values from Rng.
// For a good benchmark, we actually not only calculate rng(), we also do something else to see how much can be done in parallel due to
// instruction parallelism. For more info, see the excellent paper on the Romu generators: http://www.romu-random.org/
template <typename Rng>
void bench(ankerl::nanobench::Bench* bench, std::string name) {
    std::random_device dev;
    Rng rng(dev());
    uint64_t x = 0;
    //
    bench
        ->run(name,
              [&]() ANKERL_NANOBENCH_NO_SANITIZE("integer") {
                  auto h = std::uniform_int_distribution<uint64_t>{}(rng);

                  // murmur3 hash finalizer of the hash value
                  h ^= h >> 33;
                  h *= 0xff51afd7ed558ccd;
                  h ^= h >> 33;
                  h *= 0xc4ceb9fe1a85ec53;
                  h ^= h >> 33;

                  x ^= h;
              })
        .doNotOptimizeAway(x);
}

} // namespace

class WyRng {
public:
    using result_type = uint64_t;

    static constexpr uint64_t(min)() {
        return 0;
    }
    static constexpr uint64_t(max)() {
        return UINT64_C(0xffffffffffffffff);
    }

    WyRng(uint64_t seed) noexcept
        : mState(seed) {}

    uint64_t operator()() noexcept {
        // static constexpr uint64_t wyp0 = UINT64_C(0xa0761d6478bd642f);
        static constexpr uint64_t wyp1 = UINT64_C(0xe7037ed1a0b428db);

        ++mState;
        return mumx(mState ^ wyp1, mState);
    }

private:
    // 128bit multiply a and b, xor high and low result
    static uint64_t mumx(uint64_t a, uint64_t b) noexcept {
        uint64_t h;
        uint64_t l = umul128(a, b, &h);
        return h ^ l;
    }

    static uint64_t umul128(uint64_t a, uint64_t b, uint64_t* high) noexcept {
#if defined(__SIZEOF_INT128__)
#    if defined(__GNUC__) || defined(__clang__)
#        pragma GCC diagnostic push
#        pragma GCC diagnostic ignored "-Wpedantic"
        using uint128_t = unsigned __int128;
#        pragma GCC diagnostic pop
#    endif

        auto result = static_cast<uint128_t>(a) * static_cast<uint128_t>(b);
        *high = static_cast<uint64_t>(result >> 64U);
        return static_cast<uint64_t>(result);

#elif (defined(_MSC_VER) && SIZE_MAX == UINT64_MAX)
#    include <intrin.h> // for __umulh
#    pragma intrinsic(__umulh)
#    ifndef _M_ARM64
#        pragma intrinsic(_umul128)
#    endif
#    ifdef _M_ARM64
        *high = __umulh(a, b);
        return ((uint64_t)(a)) * (b);
#    else
        return _umul128(a, b, high);
#    endif
#else
        uint64_t ha = a >> 32U;
        uint64_t hb = b >> 32U;
        uint64_t la = static_cast<uint32_t>(a);
        uint64_t lb = static_cast<uint32_t>(b);

        uint64_t rh = ha * hb;
        uint64_t rm0 = ha * lb;
        uint64_t rm1 = hb * la;
        uint64_t rl = la * lb;

        uint64_t t = rl + (rm0 << 32U);
        uint64_t lo = t + (rm1 << 32U);
        uint64_t c = t < rl;
        c += lo < t;
        *high = rh + (rm0 >> 32U) + (rm1 >> 32U) + c;
        return lo;
#endif
    }
    uint64_t mState;
};

class NasamRng {
public:
    using result_type = uint64_t;

    static constexpr uint64_t(min)() {
        return 0;
    }
    static constexpr uint64_t(max)() {
        return UINT64_C(0xffffffffffffffff);
    }

    NasamRng(uint64_t seed) noexcept
        : mState(seed) {}

    uint64_t operator()() noexcept {
        auto x = mState++;

        // rotr(a, r) is a 64-bit rotation of a by r bits.
        x ^= rotr(x, 25) ^ rotr(x, 47);
        x *= 0x9E6C63D0676A9A99UL;
        x ^= x >> 23 ^ x >> 51;
        x *= 0x9E6D62D06F6A9A9BUL;
        x ^= x >> 23 ^ x >> 51;

        return x;
    }

private:
    // rotate right
    template <typename T>
    static T rotr(T x, size_t k) {
        return (x >> k) | (x << (8U * sizeof(T) - k));
    }

    uint64_t mState;
};

class Sfc4 {
public:
    using result_type = uint64_t;

    static constexpr uint64_t(min)() {
        return 0;
    }
    static constexpr uint64_t(max)() {
        return UINT64_C(0xffffffffffffffff);
    }

    Sfc4(uint64_t seed) noexcept
        : mA(seed)
        , mB(seed)
        , mC(seed)
        , mCounter(1) {
        for (size_t i = 0; i < 12; ++i) {
            operator()();
        }
    }

    uint64_t operator()() noexcept {
        uint64_t tmp = mA + mB + mCounter++;
        mA = mB ^ (mB >> 11U);
        mB = mC + (mC << 3U);
        mC = rotl(mC, 24U) + tmp;
        return tmp;
    }

private:
    static constexpr uint64_t rotl(uint64_t x, unsigned k) noexcept {
        return (x << k) | (x >> (64U - k));
    }

    uint64_t mA;
    uint64_t mB;
    uint64_t mC;
    uint64_t mCounter;
};

class RomuTrio {
public:
    using result_type = uint64_t;

    static constexpr uint64_t(min)() {
        return 0;
    }
    static constexpr uint64_t(max)() {
        return UINT64_C(0xffffffffffffffff);
    }

    RomuTrio(uint64_t seed) noexcept
        : mX(seed)
        , mY(UINT64_C(0x9E6C63D0676A9A99))
        , mZ(UINT64_C(0xe7037ed1a0b428db)) {
        operator()();
    }

    uint64_t operator()() noexcept {
        uint64_t x = mX;
        uint64_t y = mY;
        uint64_t z = mZ;

        mX = UINT64_C(15241094284759029579) * z;
        mY = rotl(y - x, 12);
        mZ = rotl(z - y, 44);

        return x;
    }

private:
    static constexpr uint64_t rotl(uint64_t x, unsigned k) noexcept {
        return (x << k) | (x >> (64U - k));
    }

    uint64_t mX;
    uint64_t mY;
    uint64_t mZ;
};

class RomuDuo {
public:
    using result_type = uint64_t;

    static constexpr uint64_t(min)() {
        return 0;
    }
    static constexpr uint64_t(max)() {
        return UINT64_C(0xffffffffffffffff);
    }

    RomuDuo(uint64_t seed) noexcept
        : mX(seed)
        , mY(UINT64_C(0x9E6C63D0676A9A99)) {
        operator()();
    }

    uint64_t operator()() noexcept {
        uint64_t x = mX;

        mX = UINT64_C(15241094284759029579) * mY;
        mY = rotl(mY, 36) + rotl(mY, 15) - x;

        return x;
    }

private:
    static constexpr uint64_t rotl(uint64_t x, unsigned k) noexcept {
        return (x << k) | (x >> (64U - k));
    }

    uint64_t mX;
    uint64_t mY;
};

class RomuDuoJr {
public:
    using result_type = uint64_t;

    static constexpr uint64_t(min)() {
        return 0;
    }
    static constexpr uint64_t(max)() {
        return UINT64_C(0xffffffffffffffff);
    }

    RomuDuoJr(uint64_t seed) noexcept
        : mX(seed)
        , mY(UINT64_C(0x9E6C63D0676A9A99)) {
        for (size_t i = 0; i < 10; ++i) {
            operator()();
        }
    }

    uint64_t operator()() noexcept {
        uint64_t x = mX;

        mX = UINT64_C(15241094284759029579) * mY;
        mY = rotl(mY - x, 27);

        return x;
    }

private:
    static constexpr uint64_t rotl(uint64_t x, unsigned k) noexcept {
        return (x << k) | (x >> (64U - k));
    }

    uint64_t mX;
    uint64_t mY;
};

TEST_CASE("example_random_number_generators") {
    // perform a few warmup calls, and since the runtime is not always stable for each
    // generator, increase the number of epochs to get more accurate numbers.
    ankerl::nanobench::Bench b;
    b.title("Random Number Generators").unit("uint64_t").warmup(100).relative(true);
    b.performanceCounters(true);

    // sets the first one as the baseline
    bench<std::default_random_engine>(&b, "std::default_random_engine");
    bench<std::mt19937>(&b, "std::mt19937");
    bench<std::mt19937_64>(&b, "std::mt19937_64");
    bench<std::ranlux24_base>(&b, "std::ranlux24_base");
    bench<std::ranlux48_base>(&b, "std::ranlux48_base");
    bench<std::ranlux24>(&b, "std::ranlux24_base");
    bench<std::ranlux48>(&b, "std::ranlux48");
    bench<std::knuth_b>(&b, "std::knuth_b");
    bench<WyRng>(&b, "WyRng");
    bench<NasamRng>(&b, "NasamRng");
    bench<Sfc4>(&b, "Sfc4");
    bench<RomuTrio>(&b, "RomuTrio");
    bench<RomuDuo>(&b, "RomuDuo");
    bench<RomuDuoJr>(&b, "RomuDuoJr");
    bench<ankerl::nanobench::Rng>(&b, "ankerl::nanobench::Rng");

    // Let's create a JSON file with all the results
    std::ofstream fout("example_random_number_generators.json");
    b.render(ankerl::nanobench::templates::json(), fout);
    fout.close();

    // A nice HTML graph too!
    fout.open("example_random_number_generators.html");
    b.render(ankerl::nanobench::templates::htmlBoxplot(), fout);
    fout.close();

    // finally, a CSV file for data reuse.
    fout.open("example_random_number_generators.csv");
    b.render(ankerl::nanobench::templates::csv(), fout);
    fout.close();

    // just generate a very simple overview of the results
    b.render("\n{{#result}}{{median(elapsed)}} for {{name}}\n{{/result}}", std::cout);
}
