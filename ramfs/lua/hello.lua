-- Example Lua script for CaladanOS
print("Hello from CaladanOS Lua!")
writefile("/example.txt", "First line")
appendfile("/example.txt", "\nAppended line")
readfile("/example.txt")
