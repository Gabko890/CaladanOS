io = import("io")

esc = "\27["
reset = esc .. "0m"

io.print(esc .. "37;40mANSI color demo" .. reset .. "\n")
io.print("\n")

io.print(esc .. "31;40mred on black" .. reset .. "\n")
io.print(esc .. "31;44mred on blue" .. reset .. "\n")
io.print(esc .. "31mred" .. reset .. "\n")
io.print(esc .. "97;44mwhite on blue" .. reset .. "\n")
io.print(esc .. "30;47mblack on white" .. reset .. "\n")
io.print(esc .. "32;40mgreen on black" .. reset .. "\n")
io.print(esc .. "33;40myellow on black" .. reset .. "\n")
io.print(esc .. "36;40mcyan on black" .. reset .. "\n")
io.print(esc .. "95;40mbright magenta on black" .. reset .. "\n")
io.print(esc .. "30;101mblack on bright red" .. reset .. "\n")

io.print("\n")
io.print("normal text after reset\n")
