local skynet = require "skynet.manager"
local socket = require "skynet.socket"

skynet.start(function ()
    skynet.newservice("debug_console", 8000)
    local sharetable = require "skynet.sharetable"
    local hotfixd = skynet.uniqueservice("hotfixd")
    skynet.fork(function ()
        while true do
            local names = skynet.call(hotfixd, "lua", "watch")
            sharetable.update(table.unpack(names))
        end
    end)
    local function dump(t)
        skynet.error("begin to dump table ...")
        for k, v in pairs(t) do
            skynet.error("k:",k,"v:",v)
        end
        skynet.error("dump table end")
    end
    local tab = sharetable.query("gamecfg/name.lua")
    dump(tab)
    skynet.dispatch("lua", function (_, source, cmd, ...)
        skynet.error("recv message from", source, cmd)
        if cmd == "share" then
            dump(tab)
        end
    end)
    skynet.register(".main")
end)
