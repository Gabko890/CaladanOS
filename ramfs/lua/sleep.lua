io = import("io")
time = import("time")

io.println("Enter milliseconds to sleep:")
ms = io.readline("> ")
io.println("Sleeping for " .. ms .. " ms...")
time.sleep(ms)
io.println("Done")
