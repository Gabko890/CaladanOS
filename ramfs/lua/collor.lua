esc = "\27["
reset = esc .. "0m"

write(esc .. "37;40mANSI color demo" .. reset .. "\n")
write("\n")

write(esc .. "31;40mred on black" .. reset .. "\n")
write(esc .. "31;44mred on blue" .. reset .. "\n")
write(esc .. "31mred" .. reset .. "\n")
write(esc .. "97;44mwhite on blue" .. reset .. "\n")
write(esc .. "30;47mblack on white" .. reset .. "\n")
write(esc .. "32;40mgreen on black" .. reset .. "\n")
write(esc .. "33;40myellow on black" .. reset .. "\n")
write(esc .. "36;40mcyan on black" .. reset .. "\n")
write(esc .. "95;40mbright magenta on black" .. reset .. "\n")
write(esc .. "30;101mblack on bright red" .. reset .. "\n")

write("\n")
write("normal text after reset\n")
