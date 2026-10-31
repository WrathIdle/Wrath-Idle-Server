WrathIdle = { ready = false, running = false, events = {}, stats = {}, error = "",
    action = "Connecting", reason = "Waiting for the WrathIdle server.", lastHello = -10 }
local W = WrathIdle
local function numbers(text)
    local result = {}
    if text:find("[^%d ]") then return nil end
    for token in text:gmatch("%d+") do
        local n = tonumber(token)
        if not n or n > 4294967295 or #result >= 256 then return nil end
        result[#result + 1] = n
    end
    return result
end
W.Numbers = numbers

function W:Refresh(modelChanged)
    if self.Paint then self:Paint(modelChanged) end
end
function W:Send(message)
    if #message > 249 then self.error = "This edit is too long."; self:Refresh(); return false end
    SendAddonMessage("WRI2", message, "WHISPER", UnitName("player"))
    return true
end
function W:Command(verb, args)
    if not self.ready or self.pending or not self.model then return end
    local fields = {verb, tostring(self.model.revision)}
    for _, n in ipairs(args or {}) do
        if type(n) ~= "number" or n < 0 or n ~= math.floor(n) or n > 4294967295 then
            self.error = "Use non-negative whole numbers."; self:Refresh(); return
        end
        fields[#fields + 1] = tostring(n)
    end
    if self:Send(table.concat(fields, " ")) then
        self.pending = GetTime(); self.error = ""; self:Refresh()
    end
end
function W:Note(message)
    self.events[#self.events + 1] = date("%H:%M:%S") .. "  " .. message
    while #self.events > 200 do table.remove(self.events, 1) end

end
function W:Receive(message)
    if #message > 249 then return end
    local verb, body = message:match("^(%S+)%s?(.*)$")
    if verb == "error" then
        self.error = body; self.pending = nil; self:Refresh(); return
    elseif verb == "event" then
        self:Note(body); self:Refresh(); return
    elseif verb == "status" then
        local revision, running, action, reason = body:match("^(%d+) (%d+) ([^|]+)|(.+)$")
        if revision and (not self.model or tonumber(revision) >= self.model.revision) then
            self.running = running == "1"; self.action = action; self.reason = reason
            self:Refresh()
        end
        return
    elseif verb == "classstate" then
        local cls,preset,decision=body:match("^(%d+)|([^|]+)|(.*)$")
        if cls then self.className=preset; self.classDecision=decision; self:Refresh() end
        return
    end
    if verb == "upgrade" then
        local kind,payload=body:match("^(%S+) (.*)$")
        if kind=="begin" then
            local n=numbers(payload)
            if n and #n==3 and self.model and n[1]==self.model.revision then
                self.upgradeIncoming={revision=n[1],page=n[2],pages=n[3],items={}}
            else self.upgradeIncoming=nil end
        elseif kind=="item" and self.upgradeIncoming then
            local fields,title=payload:match("^([^|]+)|(.+)$")
            local n=fields and numbers(fields)
            if n and #n==3 and #self.upgradeIncoming.items<8 then
                self.upgradeIncoming.items[#self.upgradeIncoming.items+1]={guid=n[1],entry=n[2],bind=n[3]==1,title=title}
            else self.upgradeIncoming=nil end
        elseif kind=="end" and self.upgradeIncoming then
            local n=numbers(payload); local incoming=self.upgradeIncoming
            if n and #n==2 and n[1]==incoming.revision and n[2]==incoming.page and
                self.model and n[1]==self.model.revision then self.upgrades=incoming; self:Refresh() end
            self.upgradeIncoming=nil
        end
        return
    end
    if verb == "travel" then
        local kind,payload=body:match("^(%S+) (.*)$")
        if kind=="begin" then
            local n=numbers(payload)
            if n and #n==3 and n[3]>0 and n[2]<n[3] and self.model and n[1]==self.model.revision then
                self.travelIncoming={revision=n[1],page=n[2],pages=n[3],destinations={}}
            else self.travelIncoming=nil end
        elseif kind=="destination" and self.travelIncoming then
            local fields,title=payload:match("^([^|]+)|(.+)$")
            local n=fields and numbers(fields)
            if n and #n==1 and n[1]>0 and #self.travelIncoming.destinations<8 then
                self.travelIncoming.destinations[#self.travelIncoming.destinations+1]={id=n[1],title=title}
            else self.travelIncoming=nil end
        elseif kind=="end" and self.travelIncoming then
            local n=numbers(payload); local pending=self.travelIncoming
            if n and #n==2 and n[1]==pending.revision and n[2]==pending.page and
                self.model and n[1]==self.model.revision then self.travel=pending; self:Refresh() end
            self.travelIncoming=nil
        end
        return
    end
    if verb == "catalog" then
        local kind, payload = body:match("^(%S+) (.*)$")
        if kind == "begin" then
            local n = numbers(payload)
            if n and #n == 3 and self.model and n[1] == self.model.revision then
                self.catalogIncoming = {revision=n[1],page=n[2],pages=n[3],quests={}}
            else self.catalogIncoming = nil end
        elseif kind == "quest" and self.catalogIncoming then
            local fields, title = payload:match("^([^|]+)|(.+)$")
            local n = fields and numbers(fields)
            if n and #n == 3 and #self.catalogIncoming.quests < 8 then
                self.catalogIncoming.quests[#self.catalogIncoming.quests+1] = {id=n[1],status=n[2],supported=n[3]==1,title=title}
            else self.catalogIncoming = nil end
        elseif kind == "end" and self.catalogIncoming then
            local n = numbers(payload)
            local pending = self.catalogIncoming
            if n and #n == 2 and n[1] == pending.revision and n[2] == pending.page and
                self.model and n[1] == self.model.revision then
                self.catalog = pending; self:Refresh()
            end
            self.catalogIncoming = nil
        end
        return
    end
    if verb == "capabilities" then self.capabilities = numbers(body); return end
    local n = numbers(body)
    if not n then return end
    if verb == "hello" and #n == 1 and n[1] ~= 2 then
        self.ready = false; self.error = "Incompatible server protocol."; self:Refresh()
    elseif verb == "begin" and #n == 1 and (not self.model or n[1] >= self.model.revision) then
        self.incoming = { revision = n[1], rules = {}, goals = {}, keep = {}, avoid = {},
            ruleCount = 0, classPolicy={1,1} }
    elseif verb == "profile" and self.incoming and #n == 13 then
        self.incoming.profile = n
    elseif verb == "rule" and self.incoming and #n == 6 and n[1] < 8 then
        local index = table.remove(n, 1) + 1
        if not self.incoming.rules[index] then self.incoming.ruleCount = self.incoming.ruleCount + 1 end
        self.incoming.rules[index] = n
    elseif verb == "policy" and self.incoming and #n == 4 then
        self.incoming.policy = n
    elseif verb == "classpolicy" and self.incoming and #n==2 and n[1]<=1 and n[2]<=1 then
        self.incoming.classPolicy=n
    elseif verb == "goal" and self.incoming and #n == 7 and #self.incoming.goals < 8 then
        if n[7] == 0 then self.incoming = nil; return end
        for _, goal in ipairs(self.incoming.goals) do
            if goal[7] == n[7] then self.incoming = nil; return end
        end
        self.incoming.goals[#self.incoming.goals + 1] = n
    elseif verb == "avoid" and self.incoming and #n == 1 and #self.incoming.avoid < 32 then
        self.incoming.avoid[#self.incoming.avoid+1] = n[1]
    elseif verb == "keep" and self.incoming and #n == 1 and #self.incoming.keep < 32 then
        self.incoming.keep[#self.incoming.keep + 1] = n[1]
    elseif verb == "end" and #n == 1 and self.incoming and n[1] == self.incoming.revision then
        if self.incoming.profile and self.incoming.policy and self.incoming.ruleCount == 8 then
            self.model = self.incoming; self.ready = true; self.pending = nil
            self.catalog = nil; self.catalogIncoming = nil; self.upgrades=nil; self.upgradeIncoming=nil
            self.travel=nil; self.travelIncoming=nil
            self:Refresh(true)
        end
        self.incoming = nil
    elseif verb == "stats" and #n == 15 then
        self.stats = n; self:Refresh()
    end
end

local events = CreateFrame("Frame")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:RegisterEvent("PLAYER_LEAVING_WORLD")
events:RegisterEvent("CHAT_MSG_ADDON")
events:SetScript("OnEvent", function(_, event, ...)
    if event == "PLAYER_ENTERING_WORLD" then
        W.inWorld = true; W.lastHello = -10
    elseif event == "PLAYER_LEAVING_WORLD" then
        W.inWorld = false; W.ready = false; W.pending = nil; W.incoming = nil
        W.travelIncoming=nil; W.travel=nil
        W.className=nil; W.classDecision=nil
    elseif event == "CHAT_MSG_ADDON" then
        local prefix, message, channel, sender = ...
        if prefix == "WRI2" and channel == "WHISPER" and sender == UnitName("player") then
            W:Receive(message)
        end
    end
end)
events:SetScript("OnUpdate", function()
    local now = GetTime()
    if W.inWorld and not W.ready and now - W.lastHello >= 5 then
        W.lastHello = now; W:Send("hello 2")
    end
    if W.pending and now - W.pending >= 10 then
        W.pending = nil; W.ready = false
        W.error = "Server did not acknowledge the edit. Refreshing before you retry."
        W.lastHello = -10; W:Refresh()
    end
end)
