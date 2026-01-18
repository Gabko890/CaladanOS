-- Example Lua-like script for CaladanOS
print("Hello from CaladanOS mini-lua!")
writefile("/example.txt", "First line")
appendfile("/example.txt", "\nAppended line")
readfile("/example.txt")
-- exit stops interpreter (optional)
exit()

