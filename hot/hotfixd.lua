local skynet = require "skynet"
local cache = require "skynet.codecache"
cache.mode "OFF"

local sharetable = require "skynet.sharetable"

local lfs = require "lfs"

local CMD = {}
local watch_resp = {}

local ROOT = skynet.getenv "root"

local function Cache(func)
    return setmetatable({}, {
        __mode = "kv",
        __index = func,
    })
end

local cache_file_path = Cache(function (t, k)
	local file_path = ROOT..k
	t[k] = file_path
	return file_path
end)

local function dispatch_response(...)
    for _, res in ipairs(watch_resp) do
        res(true, ...)
    end
    watch_resp = {}
end

function CMD.watch()
    watch_resp[#watch_resp+1] = skynet.response()
end

local file_status = {
    ["gamecfg/name.lua"] = 0,
}

local function check_file_status()
    while true do
        local changes = {}
		for name, time in pairs(file_status) do
			local file_path = cache_file_path[name]
			repeat
				local modtime, err = lfs.attributes(file_path, "modification")
				if not modtime then
					print(err)
					break
				end
                if modtime > time then
                    print(name, "重新加载 正在热更新")
					file_status[name] = modtime
                    sharetable.loadfile(name)
                    changes[#changes+1] = name
				end
			until true
        end
        if next(changes) then
            dispatch_response(changes)
        end
		skynet.sleep(500)
	end
end

skynet.start(function ()
    for name, _ in pairs(file_status) do
        sharetable.loadfile(name)
		file_status[name] = lfs.attributes(cache_file_path[name], "modification")
    end
    skynet.fork(check_file_status)
    skynet.dispatch("lua", function (_, _, cmd, args)
        local func = assert(CMD[cmd])
        func(args)
    end)
end)