import os
from collections import Counter

LABEL_DIR = "train/labels"  # or "valid/labels", etc.
counter = Counter()

for file in os.listdir(LABEL_DIR):
    if not file.endswith(".txt"):
        continue
    with open(os.path.join(LABEL_DIR, file)) as f:
        for line in f:
            class_id = line.strip().split()[0]
            counter[class_id] += 1

print("Class usage count:")
for cls_id, count in sorted(counter.items()):
    print(f"  Class {cls_id}: {count} labels")

if '1' not in counter:
    print("❌ No class 1 (Head) found. Model trained as single-class (Body only).")
else:
    print("✅ Class 1 (Head) exists. Something else went wrong.")
