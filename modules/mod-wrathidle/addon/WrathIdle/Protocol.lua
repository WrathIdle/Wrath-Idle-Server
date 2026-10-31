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
    SendAddonMessage("WRI1", message, "WHISPER", UnitName("player"))
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
    WrathIdleHistory = self.events
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
        if revision then
            self.running = running == "1"; self.action = action; self.reason = reason
            self:Refresh()
        end
        return
    end
    local n = numbers(body)
    if not n then return end
    if verb == "hello" and #n == 1 and n[1] ~= 1 then
        self.ready = false; self.error = "Incompatible server protocol."; self:Refresh()
    elseif verb == "begin" and #n == 1 then
        self.incoming = { revision = n[1], rules = {}, goals = {}, keep = {}, ruleCount = 0 }
    elseif verb == "profile" and self.incoming and #n == 13 then
        self.incoming.profile = n
    elseif verb == "rule" and self.incoming and #n == 6 and n[1] < 8 then
        local index = table.remove(n, 1) + 1
        if not self.incoming.rules[index] then self.incoming.ruleCount = self.incoming.ruleCount + 1 end
        self.incoming.rules[index] = n
    elseif verb == "goal" and self.incoming and #n == 6 and #self.incoming.goals < 8 then
        self.incoming.goals[#self.incoming.goals + 1] = n
    elseif verb == "keep" and self.incoming and #n == 1 and #self.incoming.keep < 32 then
        self.incoming.keep[#self.incoming.keep + 1] = n[1]
    elseif verb == "end" and #n == 1 and self.incoming and n[1] == self.incoming.revision then
        if self.incoming.profile and self.incoming.ruleCount == 8 then
            self.model = self.incoming; self.ready = true; self.pending = nil
            self:Refresh(true)
        end
        self.incoming = nil
    elseif verb == "stats" and #n == 15 then
        self.stats = n; self:Refresh()
    end
end

local events = CreateFrame("Frame")
events:RegisterEvent("ADDON_LOADED")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:RegisterEvent("PLAYER_LEAVING_WORLD")
events:RegisterEvent("CHAT_MSG_ADDON")
events:SetScript("OnEvent", function(_, event, ...)
    if event == "ADDON_LOADED" then
        local name = ...
        if name == "WrathIdle" then
            W.events = type(WrathIdleHistory) == "table" and WrathIdleHistory or {}
            while #W.events > 200 do table.remove(W.events, 1) end
            W:Note("Connected client session; server decides whether the routine is paused.")
        end
    elseif event == "PLAYER_ENTERING_WORLD" then
        W.inWorld = true; W.lastHello = -10
    elseif event == "PLAYER_LEAVING_WORLD" then
        W.inWorld = false; W.ready = false; W.pending = nil; W.incoming = nil
    elseif event == "CHAT_MSG_ADDON" then
        local prefix, message, channel, sender = ...
        if prefix == "WRI1" and channel == "WHISPER" and sender == UnitName("player") then
            W:Receive(message)
        end
    end
end)
events:SetScript("OnUpdate", function()
    local now = GetTime()
    if W.inWorld and not W.ready and now - W.lastHello >= 5 then
        W.lastHello = now; W:Send("hello 1")
    end
    if W.pending and now - W.pending >= 10 then
        W.pending = nil; W.ready = false
        W.error = "Server did not acknowledge the edit. Refreshing before you retry."
        W.lastHello = -10; W:Refresh()
    end
end)
