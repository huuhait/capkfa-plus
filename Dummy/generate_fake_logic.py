import random
import uuid
import sys

MIN_CLASSES = 100
MAX_CLASSES = 200
MIN_METHODS_PER_CLASS = 5
MAX_METHODS_PER_CLASS = 20
MIN_GLOBALS = 10
MAX_GLOBALS = 70
MAX_COMBO_FUNCTIONS = 20
MIN_TEMPLATES = 2
MAX_TEMPLATES = 7

NOISE_OPERATIONS = [
    "volatile int x = rand() % 100; for(int i = 0; i < x; ++i) {{ x += i; }}",
    "if(rand() % 2) {{ _ReadWriteBarrier(); }} else {{ volatile int y = 42; y *= 2; }}",
    "try {{ int z = rand() % 50; if(z > 25) throw z; }} catch(...) {{}}",
    "constexpr char kStr[] = \"nonsense\"; volatile int len = sizeof(kStr);",
    "volatile double d = rand() % 100 / 3.14; for(int i = 0; i < 10; ++i) {{ d *= 1.1; }}",
    "if(rand() % 3) {{ volatile int a = 7; a <<= 2; }} else {{ _ReadWriteBarrier(); }}",
    "constexpr int dummy = (123 * 456) % 789; volatile int unused = dummy;",
    "volatile int fake = rand() % 50; if(fake > 25) {{ fake *= 2; }} else {{ fake /= 2; }}"
]

def generate_method_name():
    return f"noise_{uuid.uuid4().hex[:8]}"

def generate_class_name():
    return f"FakeClass_{uuid.uuid4().hex[:6]}"

def generate_method_body():
    num_ops = random.randint(2, 5)
    selected_ops = random.sample(NOISE_OPERATIONS, num_ops)
    return "\n    ".join(selected_ops)

def generate_class(num_methods):
    class_name = generate_class_name()
    methods = []
    for _ in range(num_methods):
        method_name = generate_method_name()
        method_code = f"""
void {method_name}() {{
    {generate_method_body()}
}}
inline static const auto obf_{method_name} = $om("{method_name}", &{class_name}::{method_name}, {class_name}, void);
"""
        methods.append((method_name, method_code))
    class_code = f"""
class {class_name} {{
public:
{"".join(method[1] for method in methods)}
}};
"""
    return class_name, methods, class_code

def generate_global():
    if random.choice([True, False]):
        return f"static volatile int g_{uuid.uuid4().hex[:6]} = rand() % 1000;"
    else:
        str_id = uuid.uuid4().hex[:8]
        return f"static const auto g_{uuid.uuid4().hex[:6]} = $o(\"obfuscated_{str_id}\");"

def generate_template():
    return f"""
template<typename T>
static T obfuscated_noise_{uuid.uuid4().hex[:6]}(T x) {{
    volatile T y = x;
    _ReadWriteBarrier();
    return y + (rand() % 10);
}}
"""

def generate_combo_function(class_methods, combo_num):
    calls = []
    for i, (class_name, method_name) in enumerate(random.sample(class_methods, min(20, len(class_methods)))):
        # Unique instance name per call
        calls.append(f"{class_name} instance_{i}; $call({class_name}::obf_{method_name}, &instance_{i});")
    return f"""
static void run_fake_combo_{combo_num}() {{
    {"; ".join(calls)}
}}
"""

def generate_noise_code(output_path):
    num_classes = random.randint(MIN_CLASSES, MAX_CLASSES)
    num_globals = random.randint(MIN_GLOBALS, MAX_GLOBALS)
    num_combos = random.randint(3, MAX_COMBO_FUNCTIONS)
    num_templates = random.randint(MIN_TEMPLATES, MAX_TEMPLATES)

    classes, all_methods, class_codes = [], [], []
    for _ in range(num_classes):
        num_methods = random.randint(MIN_METHODS_PER_CLASS, MAX_METHODS_PER_CLASS)
        class_name, methods, class_code = generate_class(num_methods)
        classes.append(class_name)
        all_methods.extend([(class_name, method_name) for method_name, _ in methods])
        class_codes.append(class_code)

    globals_code = "\n".join(generate_global() for _ in range(num_globals))
    templates_code = "\n".join(generate_template() for _ in range(num_templates))
    combo_functions = [generate_combo_function(all_methods, i) for i in range(num_combos)]

    code = f"""
#pragma once
#include "Obfuscate.h"
#include <intrin.h>

{templates_code}

{globals_code}

{"".join(class_codes)}

{"".join(combo_functions)}
"""
    with open(output_path, 'w') as f:
        f.write(code)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python script.py <output_file>")
        sys.exit(1)
    generate_noise_code(sys.argv[1])