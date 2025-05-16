local c = require "zset.core"

local _M = {}

function _M:zadd(score, member)
    if not self[member] then
        self.__ud__:update({}, {score, member})
        self[member] = score
    else
        local oldScore = self[member]
        if self.__ud__:update_score(member, oldScore, oldScore+score) then
            self[member] = oldScore+score
        end

    end
end

function _M:zrem(member)
    local score = self[member]
    if not score then
        return false
    end
    self.__ud__:update({score, member}, {})
    self[member] = nil
    return true
end

function _M:zmadd(tab)
    local adds = {}
    for k, v in pairs(tab) do
        adds[#adds+1] = v
        adds[#adds+1] = k
        self[k]=v
    end
    self.__ud__:update({}, adds)
end

function _M:zmrem(tab)
    local rems = {}
    for _, member in ipairs(tab) do
        if self[member] then
            rems[#rems+1] = self[member]
            rems[#rems+1] = member
            self[member] = nil
        end
    end
    self.__ud__:update(rems, {})
end

function _M:zremrange(start, stop)
    self.__ud__:delete_by_rank(start, stop, function (member)
        self[member] = nil
    end)
end

function _M:zcount()
    return self.__ud__:get_count()
end

function _M:zrank(member)
    return self.__ud__:get_rank(self[member], member)
end

function _M:zrange(start, stop)
    return self.__ud__:get_rank_range(start, stop)
end

function _M:zget_zset_data()
    return self.__ud__:get_zset_data()
end

function _M:dump()
    self.__ud__:dump()
end

function _M:zrangebyscore(score1, score2)
    return self.__ud__:get_score_range(score1, score2)
end

function _M:getlud()
    return self.__lud__
end

function _M.new(zspoint)
    local ud, lud, zset = c.new(zspoint)
    if not zset then zset = {} end
    zset.__ud__ = ud
    zset.__lud__ = lud
    return setmetatable(zset, {__index = _M})
end

return _M
