#pragma once
#include <cstddef>
#include <array>
#include <algorithm>
#include <string_view>
#include <utility>

namespace Obfuscate {

    // CMake-defined seed, fallback if not set
    #ifndef OBF_SEED
    #define OBF_SEED 123
    #endif

    // Per-string mutation logic
    #define _OBF_KEY ((OBF_SEED + __COUNTER__) & 0x7F)
    #define _OBF_FN(i, k) ((k * 13 + i * 73) ^ (i % 7))

    constexpr char obfuscate_char(char c, std::size_t i, char k) {
        return c ^ _OBF_FN(i, k);
    }

    constexpr char deobfuscate_char(char c, std::size_t i, char k) {
        return c ^ _OBF_FN(i, k);
    }

    template<std::size_t N, char KEY>
    struct Obf {
        static constexpr std::size_t RealSize = N - 1;
        std::array<char, RealSize> data{};

        constexpr Obf(const char(&str)[N]) {
            for (std::size_t i = 0; i < RealSize; ++i)
                data[i] = obfuscate_char(str[i], i, KEY);
        }

        // One-time decrypt to buffer
        void decrypt_once(char* out) const {
            for (std::size_t i = 0; i < RealSize; ++i)
                out[i] = deobfuscate_char(data[i], i, KEY);
            out[RealSize] = '\0';
        }

        // One-shot lambda invocation with auto-wipe
        template<typename Fn>
        void use_once(Fn&& fn) const {
            char buf[RealSize + 1];
            decrypt_once(buf);
            fn(buf);
            std::fill(std::begin(buf), std::end(buf), 0);
        }
    };

    // Function obfuscation wrapper
    template<typename Fn, std::size_t N, char KEY>
    struct ObfFunc;

        template<typename Ret, typename... Args, std::size_t N, char KEY>
        struct ObfFunc<Ret(*)(Args...), N, KEY> {
            using FnPtr = Ret(*)(Args...);
            std::array<char, N - 1> data;
            FnPtr realFunc;

            constexpr ObfFunc(const char(&name)[N], FnPtr func) : realFunc(func) {
                for (std::size_t i = 0; i < N - 1; ++i)
                    data[i] = obfuscate_char(name[i], i, KEY);
            }

            std::string name() const {
                char out[N];
                for (std::size_t i = 0; i < N - 1; ++i)
                    out[i] = deobfuscate_char(data[i], i, KEY);
                out[N - 1] = '\0';
                return std::string(out);
            }

            Ret operator()(Args... args) const {
                return realFunc(std::forward<Args>(args)...);
            }
        };

    template<std::size_t N, char KEY, typename Class, typename Ret, typename... Args>
    struct ObfMemberFunc {
        using FnPtr = Ret(Class::*)(Args...);

        std::array<char, N - 1> data;
        FnPtr realFunc;

        constexpr ObfMemberFunc(const char(&name)[N], FnPtr func) : realFunc(func) {
            for (std::size_t i = 0; i < N - 1; ++i)
                data[i] = obfuscate_char(name[i], i, KEY);
        }

        std::string name() const {
            char out[N];
            for (std::size_t i = 0; i < N - 1; ++i)
                out[i] = deobfuscate_char(data[i], i, KEY);
            out[N - 1] = '\0';
            return std::string(out);
        }

        Ret invoke(Class* instance, Args... args) const {
            return (instance->*realFunc)(std::forward<Args>(args)...);
        }

        Ret operator()(Class* instance, Args... args) const {
            return invoke(instance, std::forward<Args>(args)...);
        }
    };
} // namespace Obfuscate


#define $o(str) ::Obfuscate::Obf<sizeof(str), _OBF_KEY>(str)

// Scoped one-time use macro: $d(obf, name)
#define $d(obf, name) \
    char name[decltype(obf)::RealSize + 1]; \
    obf.decrypt_once(name)

// Lambda-inline decrypt: $d_inline(obf) => returns std::string_view
#define $d_inline(obf) [&]() { \
    char _buf[decltype(obf)::RealSize + 1]; \
    obf.decrypt_once(_buf); \
    std::string_view sv(_buf); \
    std::fill(std::begin(_buf), std::end(_buf), 0); \
    return sv; \
}()

// Function obfuscation macro
#define $of(name, func) \
    ::Obfuscate::ObfFunc<decltype(+func), sizeof(name), _OBF_KEY>(name, +func)

#define $om(name, method_ptr, ClassType, RetType, ...) \
::Obfuscate::ObfMemberFunc<sizeof(name), _OBF_KEY, ClassType, RetType, __VA_ARGS__>(name, method_ptr)
#define $call(obf_mem_func, instance, ...) \
obf_mem_func(instance, ##__VA_ARGS__)
