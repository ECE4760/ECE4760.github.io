binary_string = """
[PASTE YOUR BINARY STRING HERE WITHOUT MODIFICATIONS]
""".replace("\n", "").replace(" ", "")

# Split into chunks of 64 bits
chunks = [binary_string[i:i+64] for i in range(0, len(binary_string), 64)]

# Convert each chunk to a uint64 integer
uint64_values = [int(chunk, 2) for chunk in chunks]

# Print the results
for i, val in enumerate(uint64_values):
    print(f"{i}: {val}")
