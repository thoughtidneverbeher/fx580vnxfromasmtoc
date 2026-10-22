-- xcheck.lua - run the same fixed function calls as tests/xcheck.c in CasioEmu.
--
-- The startup script is executed before Chipset::Reset(); we register a
-- post-tick hook, wait for the reset to have executed its first instruction,
-- and from then on drive the CPU ourselves: set up the registers/stack, jump
-- to the ROM function with the sentinel return address, and when it returns
-- print (result, RAM hash) in the same format as the C harness.

local SENT = 0xA1D0

local function putstr(a, s)
    for i = 1, #s do
        data[a + i - 1] = string.byte(s, i)
    end
    data[a + #s] = 0
end

local cases = {
    {name = "strlen_n", addr = 0x29A2C, er = {0xD200}, setup = function() putstr(0xD200, "hello") end},
    {name = "byte_strlen_n", addr = 0x2AB26, er = {0xD200}, setup = function() putstr(0xD200, "hello") end},
    {name = "strcpy_nn", addr = 0x0E5C8, er = {0xD300, 0xD200}, setup = function() putstr(0xD200, "hello"); putstr(0xD300, "abc") end},
    {name = "strcat_nn", addr = 0x2EDA0, er = {0xD300, 0xD200}, setup = function() putstr(0xD200, "hello"); putstr(0xD300, "abc") end},
    {name = "memcpy_nn", addr = 0x26FFA, er = {0xD300, 0xD200}, stk = {7}, setup = function()
        for i = 0, 31 do data[0xD200 + i] = string.byte("A") + i end end},
    {name = "memmove_fwd", addr = 0x0A8AA, er = {0xD300, 0xD200}, stk = {8}, setup = function()
        for i = 0, 31 do data[0xD200 + i] = 0x40 + i end end},
    {name = "memmove_bwd", addr = 0x0A8AA, er = {0xD200, 0xD204}, stk = {8}, setup = function()
        for i = 0, 31 do data[0xD200 + i] = 0x40 + i end end},
    {name = "memset_n", addr = 0x1EB94, er = {0xD300, 0xAA}, stk = {9}, setup = function() putstr(0xD200, "hello") end},
    {name = "memzero", addr = 0x09D34, er = {0xD300, 5}, setup = function() putstr(0xD200, "hello") end},
    {name = "smart_strcpy_null", addr = 0x203C2, er = {0x0000, 0xD200}, setup = function() putstr(0xD200, "hello") end},
    {name = "smart_strcpy_empty", addr = 0x203C2, er = {0xD300, 0x0000}, setup = function() putstr(0xD200, "hello"); putstr(0xD300, "abc") end},
    {name = "smart_strcat_null", addr = 0x203D6, er = {0xD300, 0x0000}, setup = function() putstr(0xD200, "hello"); putstr(0xD300, "abc") end},
    {name = "smart_strcat_dstnull", addr = 0x203D6, er = {0x0000, 0xD200}, setup = function() putstr(0xD200, "hello"); putstr(0xD300, "abc") end},
    {name = "smart_strlen_null", addr = 0x203B8, er = {0x0000}, setup = function() putstr(0xD200, "hello") end},
    {name = "strlen_empty", addr = 0x29A2C, er = {0xD200}, setup = function() data[0xD200] = 0 end},
    {name = "strcat_empty", addr = 0x2EDA0, er = {0xD300, 0xD200}, setup = function() data[0xD200] = 0; putstr(0xD300, "abc") end},
    {name = "memmove_same", addr = 0x0A8AA, er = {0xD200, 0xD200}, stk = {8}, setup = function()
        for i = 0, 31 do data[0xD200 + i] = 0x40 + i end end},
    {name = "memcpy_zero", addr = 0x26FFA, er = {0xD300, 0xD200}, stk = {0}, setup = function()
        for i = 0, 31 do data[0xD200 + i] = string.byte("A") + i end end},
}

local function hash()
    local h = 2166136261
    for a = 0xD000, 0xEBFF do
        h = ((h ~ data[a]) * 16777619) & 0xFFFFFFFF
    end
    return h
end

local state = 0      -- 0 = waiting for reset, 1 = set up next case, 2 = running
local cur = 1

emu:post_tick(function()
    if state == 0 then
        -- the reset tick executes the first instruction at 0xA1CE, leaving
        -- pc = 0xA1D2, sp = 0xF000, csr = 0
        if cpu.pc ~= 0xA1D2 or cpu.sp ~= 0xF000 or cpu.csr ~= 0 then
            return
        end
        state = 1
    end

    if state == 1 then
        local c = cases[cur]
        for a = 0xD000, 0xEFFF do
            data[a] = (a * 37 + 11) & 0xFF
        end
        c.setup()
        for i = 0, 15 do cpu["r" .. i] = 0 end
        for i = 1, #c.er do
            cpu["r" .. (2 * (i - 1))] = c.er[i] & 0xFF
            cpu["r" .. (2 * (i - 1) + 1)] = (c.er[i] >> 8) & 0xFF
        end
        cpu.sp = 0xED00
        if c.stk then
            for i = 1, #c.stk do
                data[0xED00 + 2 * (i - 1)] = c.stk[i] & 0xFF
                data[0xED00 + 2 * (i - 1) + 1] = (c.stk[i] >> 8) & 0xFF
            end
        end
        cpu.psw = 0
        cpu.dsr = 0
        cpu.ea = 0
        cpu.lr = SENT
        cpu.lcsr = 0
        cpu.pc = c.addr & 0xFFFF
        cpu.csr = (c.addr >> 16) & 0xF
        state = 2
        return
    end

    if state == 2 then
        if cpu.pc == SENT and cpu.csr == 0 then
            local ret = ((cpu.r1 & 0xFF) << 8) | (cpu.r0 & 0xFF)
            print(string.format("CASE %s RET %04X HASH %08X", cases[cur].name, ret, hash()))
            cur = cur + 1
            if cur > #cases then
                os.exit(0)
            end
            state = 1
        end
    end
end)
