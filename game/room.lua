local skynet = require "skynet"

local socket = require "skynet.socket"

local zset = require "zset"

local CMD = {}

local roles = {}

local rank = {}

local redisd

local game = {
    random_value = 0,
    user_turn = 0,
    up_limit = 10000,
    down_limit = 1,
    turns = {},
}

local function sendto(clientfd, arg)
    -- local ret = tconcat({"fd:", clientfd, arg}, " ")
    -- socket.write(clientfd, ret .. "\n")
    if not arg then
        arg = "not ok"
    end
    socket.write(clientfd, arg .. "\n")
end

local function broadcast(msg)
    for _, role in pairs(roles) do
        if role.isonline > 0 then
            sendto(role.fd, msg)
        end
    end
end

local function initRank()
    if next(rank) == nil then
        rank = zset.new()
    end
end

function CMD.start(members)
    initRank();
    for _, role in ipairs(members) do
        role.isonline = 1
        roles[role.name] = role
        game.turns[#game.turns+1] = role.name
    end
    game.random_value = math.random(1, 10000)
    broadcast(("房间:%d 系统已经随机一个数字"):format(skynet.self()))
    local rv = math.random(1, 1500)
    if rv <= 500 then
        game.user_turn = 1
    elseif rv <= 1000 then
        game.user_turn = 2
    else
        game.user_turn = 3
    end
    local name = game.turns[game.user_turn]
    broadcast(("请玩家%s开始猜数字"):format(name))
end

function CMD.offline(name)
    if roles[name] then
        roles[name].isonline = 0
        broadcast(("%s 玩家已经掉线，请求呼叫他上线"):format(name))
    end
    skynet.retpack()
end

function CMD.online(client)
    local name = client.name
    if roles[name] then
        roles[name] = client
        roles[name].isonline = 1
        broadcast(("%s 玩家已经上线"):format(name))
        sendto(client.fd, ("范围变为 [%d - %d], 接下来由 %s 来操作"):format(game.down_limit, game.up_limit, game.turns[game.user_turn]))
    end
    skynet.retpack()
end

local function game_over()
    for _, role in pairs(roles) do
        if role.isonline == 0 then
            skynet.call(redisd, "hset", "role:"..role.name, "isgame", 0)
        else
            skynet.send(role.agent, "lua", "game_over")
            sendto(role.fd, "离开房间")
        end
    end
    skynet.fork(skynet.exit)
end

function update_score(role, addscore)
    if not role then
        return
    end
    rank:zadd(addscore, role.name)
end

function CMD.guess(name, val)
    local role = assert(roles[name])
    if game.turns[game.user_turn] ~= name then
        sendto(role.fd, ("错误：还没轮到你操作，现在由 %s 来操作"):format(game.turns[game.user_turn]))
        return
    end
    if not val or val < game.down_limit or val > game.up_limit then
        sendto(role.fd, ("错误：请输入[%d - %d]之间的数字"):format(game.down_limit, game.up_limit))
        return
    end
    game.user_turn = game.user_turn % 3+1
    local next = game.turns[game.user_turn]
    if val == game.random_value then
        broadcast(("游戏结束，%s猜中了数字%d，输了"):format(name, val))
        game_over()
        return
    end
    if val < game.random_value then
        game.down_limit = val+1
        if game.down_limit == game.up_limit then
            broadcast(("游戏结束，只剩下一个数字%d %s输了"):format(val+1, next))
            game_over()
            return
        end
        update_score(role, 1)
        broadcast(("%s输入的数字太小，范围变为 [%d - %d], 接下来由 %s 来操作"):format(name, game.down_limit, game.up_limit, next))
        return
    end
    if val > game.random_value then
        game.up_limit = val-1
        if game.down_limit == game.up_limit then
            broadcast(("游戏结束，只剩下一个数字%d %s输了"):format(val-1, next))
            game_over()
            return
        end
        update_score(role, 1)
        broadcast(("%s输入的数字太大，范围变为 [%d - %d], 接下来由 %s 来操作"):format(name, game.down_limit, game.up_limit, next))
        return
    end
end

function CMD.getAllRanks(name)
    local role = assert(roles[name])
    local allRanks = rank:zget_zset_data()
    local result
    if type(allRanks) == "table" and #allRanks > 0 then
        local parts = {}
        for i = 1, #allRanks, 2 do
            local username = allRanks[i]
            local score = allRanks[i+1]
            parts[#parts + 1] = string.format("username:%s,score:%s;", username, score)
        end
        result = table.concat(parts)
    end

    sendto(role.fd, result)
end

skynet.start(function ()
    math.randomseed(math.tointeger(skynet.time()*100), skynet.self())
    redisd = skynet.uniqueservice("redis")
    skynet.dispatch("lua", function (_, _, cmd, ...)
        local func = CMD[cmd]
        if not func then
            return
        end
        func(...)
    end)
end)
