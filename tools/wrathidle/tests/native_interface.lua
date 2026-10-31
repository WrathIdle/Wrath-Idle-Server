-- Offline frame API smoke check; layout and native rendering remain user tests.
dofile("tools/wrathidle/tests/native_protocol.lua")
UISpecialFrames = {}
local widgets = {}
local methods = {}
local function widget(name)
    local f = {scripts={}, name=name, shown=true}
    setmetatable(f, {__index=function(_, key)
        return methods[key] or function() end
    end})
    widgets[#widgets+1] = f
    if name then _G[name] = f end
    return f
end
function methods:SetScript(event, callback) self.scripts[event]=callback end
function methods:SetText(value) self.value=value end
function methods:GetText() return self.value or "" end
function methods:GetName() return self.name end
function methods:CreateFontString() return widget() end
function methods:Show() self.shown=true end
function methods:Hide() self.shown=false end
function methods:IsShown() return self.shown end
function methods:Enable() self.enabled=true end
function methods:Disable() self.enabled=false end
function methods:AddMessage(value) self.lastMessage=value end
function CreateFrame(_,name) return widget(name) end
function UnitHealth() return 100 end
function UnitHealthMax() return 120 end
function UnitPower() return 20 end
function UnitPowerMax() return 100 end
function UnitLevel() return 2 end
function UnitRace() return "Human" end
function UnitClass() return "Warrior" end
function GetItemInfo() return nil end
SlashCmdList = {}
GameFontHighlightSmall = "font"
dofile("modules/mod-wrathidle/native/Interface/FrameXML/WrathIdle/Interface.lua")
WrathIdle.model.goals={
    {0,6,1,1,0,0,1},{1,2589,20,1,0,0,2},{2,7,1,1,0,0,3},
    {3,2,1,1,0,0,4},{4,1,1,1,0,0,5}}
WrathIdle:Paint(true)
assert(WrathIdleActivity and WrathIdleCommandCenter)
assert(SlashCmdList.WRATHIDLE)
local queued
WrathIdle.Command=function(_,verb,args) queued={verb,args} end
for _,f in ipairs(widgets) do
    if f.value=="Queue training" then f.scripts.OnClick() end
end
assert(queued and queued[1]=="add" and queued[2][1]==4, "Training button must queue a real service goal")
queued=nil
WrathIdle.travel={page=0,pages=1,destinations={{id=12345,title="A server-provided destination"}}}
for _,f in ipairs(widgets) do
    if f.value=="Queue travel" and not queued then f.scripts.OnClick() end
end
assert(queued and queued[2][1]==5 and queued[2][2]==12345, "Travel buttons use server-provided destination identities")
queued=nil
for _,f in ipairs(widgets) do
    if f.value=="Spend earned talent points  Off" then f.scripts.OnClick() end
end
assert(queued and queued[1]=="classpolicy" and queued[2][1]==1 and queued[2][2]==1,
    "Talent policy changes preserve the combat preset setting")
queued=nil
for _,f in ipairs(widgets) do
    if f.value=="Queue level" then f.scripts.OnClick() end
end
assert(queued and queued[2][1]==0 and queued[2][2]==80, "New leveling goals default to level 80")
print("Native interface API smoke checks passed; visual acceptance remains manual.")
