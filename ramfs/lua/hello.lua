-- Example Lua script for CaladanOS
io = import("io")

io.println("Hello from CaladanOS Lua!")
io.fs.write_file("/example.txt", "First line")
io.fs.append_file("/example.txt", "\nAppended line")
io.print(io.fs.read_file("/example.txt"))
