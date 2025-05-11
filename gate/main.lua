local skynet = require "skynet"

skynet.start(function ()
    local gated = skynet.uniqueservice("gated")
    skynet.call(gated, "lua", "start", 8989)
end)