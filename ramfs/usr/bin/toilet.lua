-- toilet: small FIGlet-style banner printer for CaladanOS Lua

esc = "\27["
reset = esc .. "0m"
io = import("io")
string = import("string")

font = {
    [" "] = {"    ", "    ", "    ", "    ", "    "},
    ["?"] = {" ### ", "#   #", "  ## ", "     ", "  #  "},
    ["!"] = {"  #  ", "  #  ", "  #  ", "     ", "  #  "},
    ["."] = {"     ", "     ", "     ", "     ", "  #  "},
    [","] = {"     ", "     ", "     ", "  #  ", " #   "},
    [":"] = {"     ", "  #  ", "     ", "  #  ", "     "},
    ["-"] = {"     ", "     ", "#####", "     ", "     "},
    ["_"] = {"     ", "     ", "     ", "     ", "#####"},
    ["/"] = {"    #", "   # ", "  #  ", " #   ", "#    "},
    ["+"] = {"     ", "  #  ", "#####", "  #  ", "     "},

    ["0"] = {" ### ", "#   #", "#   #", "#   #", " ### "},
    ["1"] = {"  #  ", " ##  ", "  #  ", "  #  ", " ### "},
    ["2"] = {" ### ", "#   #", "   # ", "  #  ", "#####"},
    ["3"] = {"#### ", "    #", " ### ", "    #", "#### "},
    ["4"] = {"#   #", "#   #", "#####", "    #", "    #"},
    ["5"] = {"#####", "#    ", "#### ", "    #", "#### "},
    ["6"] = {" ### ", "#    ", "#### ", "#   #", " ### "},
    ["7"] = {"#####", "    #", "   # ", "  #  ", "  #  "},
    ["8"] = {" ### ", "#   #", " ### ", "#   #", " ### "},
    ["9"] = {" ### ", "#   #", " ####", "    #", " ### "},

    ["A"] = {" ### ", "#   #", "#####", "#   #", "#   #"},
    ["B"] = {"#### ", "#   #", "#### ", "#   #", "#### "},
    ["C"] = {" ### ", "#   #", "#    ", "#   #", " ### "},
    ["D"] = {"#### ", "#   #", "#   #", "#   #", "#### "},
    ["E"] = {"#####", "#    ", "#### ", "#    ", "#####"},
    ["F"] = {"#####", "#    ", "#### ", "#    ", "#    "},
    ["G"] = {" ### ", "#    ", "#  ##", "#   #", " ### "},
    ["H"] = {"#   #", "#   #", "#####", "#   #", "#   #"},
    ["I"] = {" ### ", "  #  ", "  #  ", "  #  ", " ### "},
    ["J"] = {"  ###", "   # ", "   # ", "#  # ", " ##  "},
    ["K"] = {"#   #", "#  # ", "###  ", "#  # ", "#   #"},
    ["L"] = {"#    ", "#    ", "#    ", "#    ", "#####"},
    ["M"] = {"#   #", "## ##", "# # #", "#   #", "#   #"},
    ["N"] = {"#   #", "##  #", "# # #", "#  ##", "#   #"},
    ["O"] = {" ### ", "#   #", "#   #", "#   #", " ### "},
    ["P"] = {"#### ", "#   #", "#### ", "#    ", "#    "},
    ["Q"] = {" ### ", "#   #", "#   #", "#  ##", " ####"},
    ["R"] = {"#### ", "#   #", "#### ", "#  # ", "#   #"},
    ["S"] = {" ####", "#    ", " ### ", "    #", "#### "},
    ["T"] = {"#####", "  #  ", "  #  ", "  #  ", "  #  "},
    ["U"] = {"#   #", "#   #", "#   #", "#   #", " ### "},
    ["V"] = {"#   #", "#   #", "#   #", " # # ", "  #  "},
    ["W"] = {"#   #", "#   #", "# # #", "## ##", "#   #"},
    ["X"] = {"#   #", " # # ", "  #  ", " # # ", "#   #"},
    ["Y"] = {"#   #", " # # ", "  #  ", "  #  ", "  #  "},
    ["Z"] = {"#####", "   # ", "  #  ", " #   ", "#####"},
}

font["a"] = font["A"]; font["b"] = font["B"]; font["c"] = font["C"]; font["d"] = font["D"]
font["e"] = font["E"]; font["f"] = font["F"]; font["g"] = font["G"]; font["h"] = font["H"]
font["i"] = font["I"]; font["j"] = font["J"]; font["k"] = font["K"]; font["l"] = font["L"]
font["m"] = font["M"]; font["n"] = font["N"]; font["o"] = font["O"]; font["p"] = font["P"]
font["q"] = font["Q"]; font["r"] = font["R"]; font["s"] = font["S"]; font["t"] = font["T"]
font["u"] = font["U"]; font["v"] = font["V"]; font["w"] = font["W"]; font["x"] = font["X"]
font["y"] = font["Y"]; font["z"] = font["Z"]

color = esc .. "36m"
rainbow = false
text_start = 1

for i = 1, argc() - 1 do
    a = arg(i)
    if a == "--help" then
        io.print("usage: toilet [--plain|--red|--green|--yellow|--blue|--magenta|--cyan|--white|--rainbow] <text>\n")
        return
    elseif a == "--plain" then color = ""; text_start = i + 1
    elseif a == "--red" then color = esc .. "31m"; text_start = i + 1
    elseif a == "--green" then color = esc .. "32m"; text_start = i + 1
    elseif a == "--yellow" then color = esc .. "33m"; text_start = i + 1
    elseif a == "--blue" then color = esc .. "34m"; text_start = i + 1
    elseif a == "--magenta" then color = esc .. "35m"; text_start = i + 1
    elseif a == "--cyan" then color = esc .. "36m"; text_start = i + 1
    elseif a == "--white" then color = esc .. "37m"; text_start = i + 1
    elseif a == "--rainbow" then rainbow = true; text_start = i + 1
    else
        break
    end
end

if text_start >= argc() then
    io.print("usage: toilet [--color] <text>\n")
    return
end

colors = {31, 33, 32, 36, 34, 35}

for row = 1, 5 do
    color_index = 1
    if color ~= "" and not rainbow then io.print(color) end
    for ai = text_start, argc() - 1 do
        word = arg(ai)
        for ci = 1, #word do
            ch = string.charat(word, ci)
            glyph = font[ch]
            if not glyph then glyph = font["?"] end
            if rainbow then
                io.print(esc)
                io.print(colors[color_index])
                io.print("m")
                color_index = color_index + 1
                if color_index > #colors then color_index = 1 end
            end
            io.print(glyph[row])
            io.print(" ")
        end
        if ai < argc() - 1 then io.print("    ") end
    end
    if color ~= "" or rainbow then io.print(reset) end
    io.print("\n")
end
