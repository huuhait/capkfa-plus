import random
import string
from pathlib import Path

def random_cert_block(label: str) -> str:
    base64_chars = string.ascii_letters + string.digits + "+/"
    lines = [f"-----BEGIN {label}-----"]
    for _ in range(random.randint(16, 32)):
        lines.append(''.join(random.choices(base64_chars, k=64)))
    lines.append(f"-----END {label}-----")
    return "\\n".join(lines)

def generate_obfuscated_dummies(count=1000, output="obf_dummies.inl"):
    dummies = []
    for i in range(count):
        label = random.choice([
            "CERTIFICATE", "PRIVATE KEY", "RSA PRIVATE KEY",
            "EC PRIVATE KEY", "ENCRYPTED CERTIFICATE"
        ])
        dummy_str = random_cert_block(label)
        line = f'static constexpr auto _obf_dummy_{i} = $o("{dummy_str}");'
        dummies.append(line)

    Path(output).write_text("\n".join(dummies), encoding="utf-8")
    print(f"[+] Generated {count} obfuscated dummy certs to {output}")

if __name__ == "__main__":
    generate_obfuscated_dummies()
