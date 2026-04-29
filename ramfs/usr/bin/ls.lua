-- ls: list directory
io = import("io")

entries = io.fs.list_dir(arg(1))
if entries then
    for i = 1, #entries do
        io.println(entries[i])
    end
end
