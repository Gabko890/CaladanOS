-- cat: print the contents of a file
io = import("io")

text = io.fs.read_file(arg(1))
if text then io.print(text) end
