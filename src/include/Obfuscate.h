#ifndef CHINESE_OBFUSCATE_HPP
#define CHINESE_OBFUSCATE_HPP

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

namespace ChineseObf {

#ifndef OBF_SEED
#define OBF_SEED 0xDEADBEEF
#endif

constexpr uint64_t mix(uint64_t x) {
    x ^= x >> 33;
    x *= 0xFF51AFD7ED558CCDULL;
    x ^= x >> 33;
    x *= 0xC4CEB9FE1A85EC53ULL;
    x ^= x >> 33;
    return x;
}

constexpr uint64_t gen_key(size_t counter) {
    return mix(OBF_SEED ^ counter);
}

constexpr std::array<std::string_view, 20> noise_strings{
    "混沌字节崩溃🌪️🧠🧨💣🔥💀👾🧟‍♂️🤯🌀🎆🧬🐉📛🐲💻🧯🔒🩸🕳️",
    "量子迷雾编码🌀🌌🔮💥🦠🧬🪐🌠👁️‍🗨️📡🛸🔬🧪⚗️🕸️🪬🪄🩻🧙‍♂️",
    "虚空指令裂变💥🕳️🪐🌌🦠🧬👾🛸🔮📡🧪⚗️🕸️🪬🪄🩻🧙‍♂️🌠🎇",
    "混沌算法漩涡🌪️🌀💻🧠🧨💣🔥💀👾🧟‍♂️🤯🎆🧬🐉📛🐲🧯🔒🩸",
    "字节幽灵缠绕🧟‍♂️👾💀🩸🕳️🌌🪐🔮📡🛸🔬🧪⚗️🕸️🪬🪄🩻🧙‍♂️",
    "加密龙焰风暴🐉🔥🌪️🧠🧨💣💀👾🧟‍♂️🤯🌀🎆🧬📛🐲💻🧯🔒🩸",
    "量子字节迷宫🧬🌀🌌🔮💥🦠🪐🌠👁️‍🗨️📡🛸🔬🧪⚗️🕸️🪬🪄🩻",
    "幽暗指令深渊🕳️🌌🪐🔮📡🛸🔬🧪⚗️🕸️🪬🪄🩻🧙‍♂️🌠🎇💥",
    "混沌代码烈焰🔥🧨💣💀👾🧟‍♂️🤯🌀🎆🧬🐉📛🐲💻🧯🔒🩸🕳️",
    "虚空算法龙息🐉🪐🌌🔮📡🛸🔬🧪⚗️🕸️🪬🪄🩻🧙‍♂️🌠🎇💥",
    "字节迷雾裂变🌫️🧬🌀🌌🔮💥🦠🪐🌠👁️‍🗨️📡🛸🔬🧪⚗️🕸️🪬",
    "幽灵指令漩涡🧟‍♂️👾💀🩸🕳️🌌🪐🔮📡🛸🔬🧪⚗️🕸️🪬🪄🩻",
    "龙焰量子深渊🐉🔥🧬🌀🌌🔮💥🦠🪐🌠👁️‍🗨️📡🛸🔬🧪⚗️🕸️",
    "混沌字节烈焰🌪️🧠🧨💣🔥💀👾🧟‍♂️🤯🌀🎆🧬🐉📛🐲💻🧯🔒",
    "虚空代码迷宫🕳️🌌🪐🔮📡🛸🔬🧪⚗️🕸️🪬🪄🩻🧙‍♂️🌠🎇💥",
    "量子指令龙息🧬🌀🌌🔮💥🦠🪐🌠👁️‍🗨️📡🛸🔬🧪⚗️🕸️🪬🪄",
    "幽暗算法风暴🧟‍♂️👾💀🩸🕳️🌌🪐🔮📡🛸🔬🧪⚗️🕸️🪬🪄🩻",
    "混沌迷雾裂变🌪️🧠🧨💣🔥💀👾🧟‍♂️🤯🌀🎆🧬🐉📛🐲💻🧯",
    "字节龙焰深渊🐉🔥🧬🌀🌌🔮💥🦠🪐🌠👁️‍🗨️📡🛸🔬🧪⚗️🕸️",
    "虚空量子漩涡🕳️🌌🪐🔮📡🛸🔬🧪⚗️🕸️🪬🪄🩻🧙‍♂️🌠🎇💥"
};

constexpr size_t noise_index(size_t counter) {
    return (mix(counter) % noise_strings.size());
}

template <size_t N, size_t Counter>
struct ObfString {
    std::array<char, N> data{};
    constexpr ObfString(const char (&str)[N]) {
        constexpr uint64_t key = gen_key(Counter);
        for (size_t i = 0; i < N; ++i) {
            data[i] = str[i] ^ (key >> ((i % 8) * 8));
        }
    }
};

template <size_t N, size_t Counter>
constexpr auto make_obf_string(const char (&str)[N]) {
    return ObfString<N, Counter>{str};
}

#define $o(str) ChineseObf::make_obf_string<sizeof(str), __COUNTER__>(str)

template <size_t N, size_t Counter>
void decrypt(const ObfString<N, Counter>& obf, char* buf) {
    constexpr uint64_t key = gen_key(Counter);
    for (size_t i = 0; i < N; ++i) {
        buf[i] = obf.data[i] ^ (key >> ((i % 8) * 8));
    }
}

#define $d(obf_str, buf) ChineseObf::decrypt(obf_str, buf)

template <size_t N, size_t Counter>
std::string_view decrypt_inline(const ObfString<N, Counter>& obf) {
    static char buf[N]{};
    decrypt(obf, buf);
    return {buf, N - 1};
}

#define $d_inline(obf_str) ChineseObf::decrypt_inline(obf_str)

template <auto Func, size_t Counter>
struct ObfFunc {
    static constexpr std::string_view name = noise_strings[noise_index(Counter)];
    static constexpr auto ptr = Func;
};

#define $of(func) ChineseObf::ObfFunc<&func, __COUNTER__>::ptr

    template <auto Method, typename ClassType, typename RetType, size_t Counter, typename... Args>
    struct ObfMethod {
        static constexpr std::string_view name = noise_strings[noise_index(Counter)];
        static constexpr auto ptr = Method;
    };

#define $om(method, ClassType, RetType, ...) \
    ChineseObf::ObfMethod<&ClassType::method, ClassType, RetType, __VA_ARGS__, __COUNTER__>::ptr

#define $om2(Method, Interface, ReturnType) \
[](Interface* obj, REFIID riid, void** ppvObj) -> ReturnType { \
return obj->Method(riid, ppvObj); \
}

#define $call(obj, method_ptr, ...) ((obj)->*(method_ptr))(__VA_ARGS__)

#define $call2(Obj, Callable, ...) Callable(Obj, __VA_ARGS__)

template <size_t N>
struct VMBytecode {
    std::array<uint8_t, N> data{};
    constexpr VMBytecode(const uint8_t (&bytes)[N]) {
        constexpr uint64_t key = gen_key(__COUNTER__);
        for (size_t i = 0; i < N; ++i) {
            data[i] = bytes[i] ^ (key >> ((i % 8) * 8));
        }
    }
};

template <size_t N, typename F>
void run_vm(const VMBytecode<N>& bytecode, F&& block) {
    constexpr uint64_t key = gen_key(__COUNTER__ - 1);
    for (size_t i = 0; i < N; ++i) {
        uint8_t instr = bytecode.data[i] ^ (key >> ((i % 8) * 8));
        block(instr);
    }
}

template <size_t N, typename F>
void run_vm_dynamic(const std::array<uint8_t, N>& bytecode, uint8_t key, F&& block) {
    for (size_t i = 0; i < N; ++i) {
        uint8_t instr = bytecode[i] ^ key;
        block(instr);
    }
}

#define OBF_VM_FUNCTION(byte_array, block) \
    do { \
        constexpr ChineseObf::VMBytecode<sizeof(byte_array)> bc(byte_array); \
        ChineseObf::run_vm(bc, block); \
    } while (false)

#define OBF_VM_FUNCTION_DYNAMIC(bytecode, key, block) \
    ChineseObf::run_vm_dynamic(bytecode, key, block)

#define VM_CASE(val) case val:

/* Example Usage:
#include <iostream>

void my_global_func(int x) {
    std::cout << "Global func: " << x << "\n";
}

struct MyClass {
    int my_method(double d) {
        std::cout << "Member func: " << d << "\n";
        return static_cast<int>(d);
    }
};

int main() {
    // String obfuscation
    auto obf_str = $o("secret_key");
    char buf[16]{};
    $d(obf_str, buf);
    std::cout << "Decrypted: " << buf << "\n";
    auto sv = $d_inline(obf_str);
    std::cout << "Inline: " << sv << "\n";

    // Global function obfuscation
    auto obf_func = $of(my_global_func);
    obf_func(42);

    // Member function obfuscation
    MyClass obj;
    auto obf_method = $om(my_method, MyClass, int, double);
    int result = $call(&obj, obf_method, 3.14);
    std::cout << "Result: " << result << "\n";

    // VM bytecode execution
    constexpr uint8_t bytecode[] = {1, 2, 3};
    OBF_VM_FUNCTION(bytecode, [](uint8_t instr) {
        switch (instr) {
            VM_CASE(1) std::cout << "Instr 1\n"; break;
            VM_CASE(2) std::cout << "Instr 2\n"; break;
            VM_CASE(3) std::cout << "Instr 3\n"; break;
        }
    });

    return 0;
}
*/

} // namespace ChineseObf

#endif // CHINESE_OBFUSCATE_HPP