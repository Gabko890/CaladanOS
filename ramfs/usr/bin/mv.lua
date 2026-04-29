-- mv: move/rename file
io = import("io")

text = io.fs.read_file(arg(1))
if text then
    io.fs.write_file(arg(2), text)
    io.fs.remove(arg(1))
end
