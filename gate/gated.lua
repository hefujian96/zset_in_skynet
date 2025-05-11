local skynet = require "skynet"

local socket = require "skynet.socket"

local CMD = {}

local netcore_tab = {}
local net_iter

local function iterator(max, start)
    start = start or 1
    if start >= max or start < 0 then
        start = 1
    end
    local i = start-1
    return function ()
        i = i % max+1
        if i == 0 then
            i = 1
        end
        return i
    end
end

local function accept(fd, ipa)
    ipa = ipa:match("([^:]+)%:.+")
    local addr = netcore_tab[net_iter()]
    skynet.send(addr, "lua", "start", {fd = fd, ipa = ipa})
end

function CMD.start(port)
    local fd = socket.listen("0.0.0.0", port)
    socket.start(fd, accept)
end

local function init_service()
    local netcore = skynet.getenv("thread") or 8
    for i=1, netcore do
        netcore_tab[#netcore_tab+1] = skynet.newservice("netd")
    end
    net_iter = iterator(#netcore_tab)
end

skynet.start(function ()
    init_service()
    skynet.dispatch("lua", function(_, _, cmd, ...)
        local f = assert(CMD[cmd])
        skynet.retpack(f(...))
    end)
end)
