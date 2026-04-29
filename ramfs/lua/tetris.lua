io = import("io")
random = import("random")
time = import("time")

esc = "\27["
reset = esc .. "0m"

board_w = 10
board_h = 18
board = {}
for y = 1, board_h do
    board[y] = {}
    for x = 1, board_w do board[y][x] = 0 end
end

pieces = {
    { color = 46, rots = {
        { {0,1}, {1,1}, {2,1}, {3,1} },
        { {2,0}, {2,1}, {2,2}, {2,3} },
    }},
    { color = 44, rots = {
        { {0,0}, {0,1}, {1,1}, {2,1} },
        { {1,0}, {2,0}, {1,1}, {1,2} },
        { {0,1}, {1,1}, {2,1}, {2,2} },
        { {1,0}, {1,1}, {0,2}, {1,2} },
    }},
    { color = 43, rots = {
        { {2,0}, {0,1}, {1,1}, {2,1} },
        { {1,0}, {1,1}, {1,2}, {2,2} },
        { {0,1}, {1,1}, {2,1}, {0,2} },
        { {0,0}, {1,0}, {1,1}, {1,2} },
    }},
    { color = 47, rots = {
        { {1,0}, {2,0}, {1,1}, {2,1} },
    }},
    { color = 42, rots = {
        { {1,0}, {2,0}, {0,1}, {1,1} },
        { {1,0}, {1,1}, {2,1}, {2,2} },
    }},
    { color = 45, rots = {
        { {1,0}, {0,1}, {1,1}, {2,1} },
        { {1,0}, {1,1}, {2,1}, {1,2} },
        { {0,1}, {1,1}, {2,1}, {1,2} },
        { {1,0}, {0,1}, {1,1}, {1,2} },
    }},
    { color = 41, rots = {
        { {0,0}, {1,0}, {1,1}, {2,1} },
        { {2,0}, {1,1}, {2,1}, {1,2} },
    }},
}

score = 0
lines = 0
frame_ms = 65
fall_tick = 0
left_repeat = 0
right_repeat = 0
down_repeat = 0
last_rotate = false
last_drop = false
last_quit = false

function rnd(max)
    return random(1, max)
end

function new_piece()
    p = rnd(#pieces)
    cur = { id = p, rot = 1, x = 4, y = 0 }
end

function cells_for(piece)
    return pieces[piece.id].rots[piece.rot]
end

function collides(piece, nx, ny, nr)
    oldx = piece.x
    oldy = piece.y
    oldr = piece.rot
    piece.x = nx
    piece.y = ny
    piece.rot = nr
    cells = cells_for(piece)
    hit = false
    for i = 1, #cells do
        bx = piece.x + cells[i][1]
        by = piece.y + cells[i][2]
        if bx < 1 or bx > board_w or by > board_h then hit = true end
        if by >= 1 and not hit and board[by][bx] ~= 0 then hit = true end
    end
    piece.x = oldx
    piece.y = oldy
    piece.rot = oldr
    return hit
end

function lock_piece()
    cells = cells_for(cur)
    for i = 1, #cells do
        bx = cur.x + cells[i][1]
        by = cur.y + cells[i][2]
        if by >= 1 and by <= board_h and bx >= 1 and bx <= board_w then
            board[by][bx] = pieces[cur.id].color
        end
    end
end

function clear_lines()
    y = board_h
    while y >= 1 do
        full = true
        for x = 1, board_w do
            if board[y][x] == 0 then full = false end
        end
        if full then
            lines = lines + 1
            score = score + 100
            for yy = y, 2, -1 do
                for x = 1, board_w do board[yy][x] = board[yy - 1][x] end
            end
            for x = 1, board_w do board[1][x] = 0 end
        else
            y = y - 1
        end
    end
end

function active_color_at(x, y)
    cells = cells_for(cur)
    for i = 1, #cells do
        bx = cur.x + cells[i][1]
        by = cur.y + cells[i][2]
        if bx == x and by == y then return pieces[cur.id].color end
    end
    return 0
end

function block(color)
    if color == 0 then return esc .. "30;40m  " .. reset end
    return esc .. "30;" .. color .. "m  " .. reset
end

function draw()
    io.print(esc .. "2J" .. esc .. "H")
    io.print(esc .. "97;40mCaladanOS Lua Tetris" .. reset .. "\n")
    io.print("Score: " .. score .. "  Lines: " .. lines .. "\n")
    io.print(esc .. "37;40mA/D move  S soft drop  W/X rotate  Space drop  Q quit" .. reset .. "\n")
    io.print("+--------------------+\n")
    for y = 1, board_h do
        io.print("|")
        for x = 1, board_w do
            c = active_color_at(x, y)
            if c == 0 then c = board[y][x] end
            io.print(block(c))
        end
        io.print("|\n")
    end
    io.print("+--------------------+\n")
end

function move(dx, dy)
    if not collides(cur, cur.x + dx, cur.y + dy, cur.rot) then
        cur.x = cur.x + dx
        cur.y = cur.y + dy
        return true
    end
    return false
end

function rotate()
    nr = cur.rot + 1
    if nr > #pieces[cur.id].rots then nr = 1 end
    if not collides(cur, cur.x, cur.y, nr) then cur.rot = nr end
end

function drop()
    while move(0, 1) do score = score + 1 end
end

function step_down()
    if not move(0, 1) then
        lock_piece()
        clear_lines()
        new_piece()
        if collides(cur, cur.x, cur.y, cur.rot) then return false end
    end
    return true
end

function is_down(key)
    return keydown ~= nil and keydown(key)
end

function repeat_pressed(key, counter)
    if not is_down(key) then return false, 0 end
    counter = counter + 1
    if counter == 1 or counter >= 5 then
        if counter >= 5 then counter = 3 end
        return true, counter
    end
    return false, counter
end

function fall_delay()
    delay = 10
    cleared = lines
    while cleared >= 4 do
        delay = delay - 1
        cleared = cleared - 4
    end
    if delay < 3 then delay = 3 end
    return delay
end

new_piece()
running = true
draw()

while running do
    left, left_repeat = repeat_pressed("a", left_repeat)
    right, right_repeat = repeat_pressed("d", right_repeat)
    down, down_repeat = repeat_pressed("s", down_repeat)
    rotating = is_down("w") or is_down("x")
    dropping = is_down("space")
    quitting = is_down("q")

    if quitting and not last_quit then
        running = false
    end
    if running and left then move(-1, 0) end
    if running and right then move(1, 0) end
    if running and rotating and not last_rotate then rotate() end
    if running and dropping and not last_drop then
        drop()
        if not step_down() then running = false end
        fall_tick = 0
    end
    if running and down then
        score = score + 1
        if not step_down() then running = false end
        fall_tick = 0
    end

    fall_tick = fall_tick + 1
    if running and fall_tick >= fall_delay() then
        if not step_down() then running = false end
        fall_tick = 0
    end

    last_rotate = rotating
    last_drop = dropping
    last_quit = quitting

    draw()
    time.sleep(frame_ms)
end

draw()
io.print(esc .. "91;40mGame over." .. reset .. " Final score: " .. score .. "\n")
