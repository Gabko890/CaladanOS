-- cp: copy file
io = import("io")

text = io.fs.read_file(arg(1))
if text then io.fs.write_file(arg(2), text) end
