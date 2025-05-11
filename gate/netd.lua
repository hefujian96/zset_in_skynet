local skynet = require "skynet.manager"

local CMD = {}
local socket_init = {}

skynet.register_protocol {
    name = "text",
    id = skynet.PTYPE_TEXT,
    pack = function(...) return ... end,
    unpack = skynet.tostring,
}

skynet.register_protocol {
    name = "client",
    id = skynet.PTYPE_CLIENT,
    pack = function(buf, sz) return buf, sz end,
}

local callback = {}
callback.D = function (source)
    print("close a connect[D]", source)
    local cli = socket_init[source]
    cli:close()
    socket_init[source] = nil
end
callback.S = function (source)
    print("connectted", source)
    local cli = socket_init[source]
    cli:dispatch()
end
callback.F = function (source)
    print("close a connect[F]", source)
end

-- 主动断开连接
local function kill(self, reason)
    print("kill", self.addr, "reason", reason)
    skynet.send(self.addr, "text", "K")
end

-- 消息解析并分发
local function dispatch_message(self)
    while true do
        local ok, msg, sz = pcall(skynet.rawcall, self.addr, "text", "R")
        if not ok then
            -- 网络已经断开
            break
        end
        local bin = skynet.tostring(msg, sz)
        local proto = string.unpack("<I4", bin)
        if proto == 0 then -- 这里是 PROXY_PROTOCOL_V1
            self.ipa = bin:sub(5)
        else
            -- 这里写具体协议解析逻辑
        end
    end
end

function CMD.start(args)
    local fd = assert(args.fd)
    local addr = skynet.launch("channel", skynet.self(), fd, 1, 0)
    socket_init[addr] = {
        addr = addr,
        fd = fd,
        ipa = args.ipa,
        dispatch = dispatch_message,
        kill = kill,
    }
end

skynet.start(function ()
    skynet.dispatch("text", function (_, source, cmd)
        callback[cmd](source)
    end)
    skynet.dispatch("lua", function (_, source, cmd, ...)
        assert(CMD[cmd])(...)
    end)
end)
