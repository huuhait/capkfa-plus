import sys

def scan_binary_for_string(filename, min_length=5):
    with open(filename, 'rb') as f:
        data = f.read()

    strings = []
    current = b""
    for b in data:
        if 32 <= b <= 126:
            current += bytes([b])
        else:
            if len(current) >= min_length:
                strings.append(current.decode('ascii', errors='ignore'))
            current = b""

    for s in strings:
        print(s)

# Usage: python scan.py MySecureApp.exe
if __name__ == "__main__":
    scan_binary_for_string(sys.argv[1])
