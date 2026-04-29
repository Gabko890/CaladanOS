io = io or {}

io.print = __c_io_print
io.println = __c_io_println
io.readline = __c_io_readline
io.getchar = __c_io_getchar
io.clear = __c_io_clear

io.fs = io.fs or {}
io.fs.exists = __c_file_exists
io.fs.is_file = __c_file_is_file
io.fs.is_dir = __c_file_is_dir
io.fs.read_file = __c_file_read_file
io.fs.write_file = __c_file_write_file
io.fs.append_file = __c_file_append_file
io.fs.list_dir = __c_file_list_dir
io.fs.mkdir = __c_file_mkdir
io.fs.remove = __c_file_remove

return io
