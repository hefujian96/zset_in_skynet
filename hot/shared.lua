local skynet = require "skynet"

local CMD = {}

local sharetable = require "skynet.sharetable"

function CMD.get()
    local tab = sharetable.query("gamecfg/name.lua")
    for k, v in pairs(tab) do
        skynet.error(k, v)
    end
end

skynet.start(function ()
    local hotfixd = skynet.uniqueservice("hotfixd")
    skynet.fork(function ()
        while true do
            local names = skynet.call(hotfixd, "lua", "watch")
            sharetable.update(table.unpack(names))
        end
    end)
    skynet.dispatch("lua", function (_, _, cmd, ...)
        local f = assert(CMD[cmd])
        f(...)
    end)
end)