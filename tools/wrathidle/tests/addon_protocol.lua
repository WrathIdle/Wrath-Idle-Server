-- Run from the repository root with Lua 5.1 (the original client's Lua version).
local clock, sent, handlers = 0, {}, {}
function GetTime() return clock end
function UnitName() return "Idlepilot" end
function date() return "12:00:00" end
function SendAddonMessage(prefix, message, channel, target)
    sent[#sent + 1] = {prefix, message, channel, target}
end
function CreateFrame()
    return { RegisterEvent = function() end, SetScript = function(_, event, callback) handlers[event] = callback end }
end
dofile("modules/mod-wrathidle/addon/WrathIdle/Protocol.lua")
local W = WrathIdle
handlers.OnEvent(nil, "PLAYER_ENTERING_WORLD")
handlers.OnUpdate()
assert(sent[1][1] == "WRI1" and sent[1][2] == "hello 1" and sent[1][4] == "Idlepilot")
local function receive(message, sender)
    handlers.OnEvent(nil, "CHAT_MSG_ADDON", "WRI1", message, "WHISPER", sender or "Idlepilot")
end
receive("begin 9", "Someoneelse")
assert(not W.incoming)
receive("begin 9"); receive("end 9")
assert(not W.ready, "Incomplete snapshot must not unlock commands")
receive("begin 9")
receive("profile 60 90 20 2 30 100 500 10 2070 1 6 45 0")
for i = 0, 7 do receive("rule " .. i .. " 0 0 100 0 0") end
receive("goal 0 6 1 1 0 32"); receive("keep 2589"); receive("end 9")
assert(W.ready and W.model.revision == 9 and #W.model.rules == 8 and W.model.keep[1] == 2589)
W:Command("resume")
assert(sent[#sent][2] == "resume 9" and W.pending == 0)
local count = #sent
W:Command("town"); assert(#sent == count, "Only one outstanding mutation")
receive("error Invalid request")
assert(not W.pending and W.error == "Invalid request")
receive("status 9 1 Combat|Fighting a wolf.")
assert(W.running and W.action == "Combat" and W.reason == "Fighting a wolf.")
receive("stats 10 50 3 0 0 1 5 0 12 100 3 0 1 1 10")
assert(W.stats[2] == 50 and W.stats[6] == 1)
W:Command("pause"); clock = 11; handlers.OnUpdate()
assert(not W.ready and not W.pending, "Timeout forces resynchronization")
handlers.OnEvent(nil, "PLAYER_LEAVING_WORLD")
count = #sent; clock = 20; handlers.OnUpdate(); assert(#sent == count)
for i = 1, 210 do W:Note("Decision " .. i) end
assert(#W.events == 200 and #WrathIdleHistory == 200)
assert(not W.Numbers("-1") and not W.Numbers("1.5") and not W.Numbers("4294967296"))
assert(loadfile("modules/mod-wrathidle/addon/WrathIdle/Interface.lua"))
print("Addon protocol checks passed: negotiation, source validation, atomic snapshots, commands, timeout, disconnect, history.")
