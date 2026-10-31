local W = WrathIdle
local function label(parent, value, x, y, width, font)
    local t = parent:CreateFontString(nil, "OVERLAY", font or "GameFontHighlight")
    t:SetPoint("TOPLEFT", x, -y); t:SetWidth(width); t:SetJustifyH("LEFT"); t:SetText(value)
    return t
end
local function panel(name, parent, width, height)
    local f = CreateFrame("Frame", name, parent)
    f:SetSize(width, height)
    f:SetBackdrop({bgFile="Interface\\DialogFrame\\UI-DialogBox-Background",
        edgeFile="Interface\\Tooltips\\UI-Tooltip-Border", tile=true, tileSize=16, edgeSize=16,
        insets={left=4,right=4,top=4,bottom=4}})
    return f
end
local controls = {}
local function button(parent, title, x, y, width, action, managed)
    local b = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
    b:SetPoint("TOPLEFT", x, -y); b:SetSize(width, 24); b:SetText(title); b:SetScript("OnClick", action)
    if managed then controls[#controls+1] = b end
    return b
end
local function edit(parent, x, y, width, default)
    local e = CreateFrame("EditBox", nil, parent, "InputBoxTemplate")
    e:SetPoint("TOPLEFT", x, -y); e:SetSize(width, 24); e:SetAutoFocus(false); e:SetMaxLetters(180)
    e:SetText(tostring(default or "")); e:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)
    e:SetScript("OnEnterPressed", function(self) self:ClearFocus() end)
    return e
end
local function integer(e)
    local s = e:GetText()
    if not s:match("^%d+$") or tonumber(s) > 4294967295 then
        W.error = "Enter a non-negative whole number."; W:Refresh(); return nil
    end
    return tonumber(s)
end

local hud = panel("WrathIdleActivity", UIParent, 740, 104)
hud:SetPoint("BOTTOM", 0, 12); hud:SetFrameStrata("HIGH")
local activity = label(hud, "WrathIdle", 16, 12, 700, "GameFontNormal")
local reason = label(hud, "Connecting to the server", 16, 33, 700)
reason:SetHeight(28); reason:SetJustifyV("TOP")
button(hud, "Start / Resume", 16, 69, 130, function() W:Command("resume") end, true)
button(hud, "Pause", 152, 69, 80, function() W:Command("pause") end, true)
button(hud, "Town services", 238, 69, 120, function() W:Command("town") end, true)
local vitals = label(hud, "", 510, 73, 210, "GameFontHighlightSmall")

local main = panel("WrathIdleCommandCenter", UIParent, 820, 568)
main:SetPoint("CENTER", 0, 35); main:SetClampedToScreen(true); main:SetFrameStrata("DIALOG")
main:EnableMouse(true); main:SetMovable(true); main:RegisterForDrag("LeftButton")
main:SetScript("OnDragStart", main.StartMoving); main:SetScript("OnDragStop", main.StopMovingOrSizing)
table.insert(UISpecialFrames, "WrathIdleCommandCenter")
label(main, "WrathIdle  |  Command center", 20, 18, 730, "GameFontNormalLarge")
local close = CreateFrame("Button", nil, main, "UIPanelCloseButton"); close:SetPoint("TOPRIGHT", -5, -5)
button(hud, "Manage", 364, 69, 100, function() if main:IsShown() then main:Hide() else main:Show() end end)
local errorText = label(main, "", 20, 46, 775, "GameFontRedSmall")
errorText:SetHeight(32)
local pages, titles = {}, {"Goals", "Quests", "Travel", "Character", "Upkeep", "Decisions"}
for i, title in ipairs(titles) do
    local index = i
    local p = CreateFrame("Frame", nil, main); p:SetPoint("TOPLEFT", 20, -112); p:SetSize(780, 430)
    pages[i] = p
    button(main, title, 20+(i-1)*130, 80, 124, function()
        for j, page in ipairs(pages) do if j==index then page:Show() else page:Hide() end end
        if index==2 and W.ready then W:Send("quests 0") end
        if index==6 and W.ready then W:Send("upgrades 0") end
    end)
    if i~=1 then p:Hide() end
end

local rows = {}
label(pages[1], "Goals run in order; recovery and services may interrupt them.", 0, 0, 770)
for i=1,8 do
    local index = i
    rows[i] = label(pages[1], "", 0, 31+(i-1)*31, 570, "GameFontHighlightSmall")
    button(pages[1], "Next", 575, 25+(i-1)*31, 70, function()
        local g = W.model and W.model.goals[index]; if g then W:Command("move", {g[7],0}) end
    end, true)
    button(pages[1], "Cancel", 655, 25+(i-1)*31, 90, function()
        local g = W.model and W.model.goals[index]; if g then W:Command("cancel", {g[7]}) end
    end, true)
end
label(pages[1], "Target level", 0, 290, 140); local level = edit(pages[1], 5, 313, 100, 80)
button(pages[1], "Queue level", 120, 313, 130, function()
    local n=integer(level); if n then W:Command("add", {0,n,1,1,0}) end
end, true)
label(pages[1], "Item ID / quantity", 290, 290, 220)
local item, count = edit(pages[1], 295, 313, 100, 2589), edit(pages[1], 410, 313, 80, 20)
button(pages[1], "Queue stock", 510, 313, 130, function()
    local id,n=integer(item),integer(count); if id and n then W:Command("add", {1,id,n,1,0}) end
end, true)
local scope = label(pages[1], "", 0, 360, 760, "GameFontNormalSmall")

local questText=label(pages[2], "Refresh to browse available quests.", 0, 0, 760)
local questRows={}
for i=1,8 do
    local index=i
    questRows[i]=label(pages[2],"",0,40+(i-1)*35,535,"GameFontHighlightSmall")
    button(pages[2],"Queue",550,34+(i-1)*35,90,function()
        local q=W.catalog and W.catalog.quests[index]
        if q then W:Command("add",{2,q.id,1,1,0}) end
    end,true)
    button(pages[2],"Avoid",650,34+(i-1)*35,90,function()
        local q=W.catalog and W.catalog.quests[index]
        if not q or not W.model then return end
        local ids={unpack(W.model.avoid)}
        for _,id in ipairs(ids) do if id==q.id then return end end
        ids[#ids+1]=q.id; W:Command("avoid",ids)
    end,true)
end
button(pages[2],"Allow avoided quests",410,340,200,function() W:Command("avoid",{}) end,true)
button(pages[2],"Refresh quests",0,340,150,function() if W.ready then W:Send("quests 0") end end)
button(pages[2],"Previous",170,340,100,function()
    if W.ready and W.catalog and W.catalog.page>0 then W:Send("quests "..(W.catalog.page-1)) end
end)
button(pages[2],"Next page",280,340,100,function()
    if W.ready and W.catalog and W.catalog.page+1<W.catalog.pages then W:Send("quests "..(W.catalog.page+1)) end
end)
local travelText=label(pages[3], "", 0, 0, 760); travelText:SetHeight(64); travelText:SetJustifyV("TOP")
local travelRows={}
for i=1,8 do
    local index=i
    travelRows[i]=label(pages[3],"",0,80+(i-1)*31,560,"GameFontHighlightSmall")
    button(pages[3],"Queue travel",580,74+(i-1)*31,160,function()
        local destination=W.travel and W.travel.destinations[index]
        if destination then W:Command("add",{5,destination.id,1,1,0}) end
    end,true)
end
button(pages[3],"Refresh destinations",0,350,190,function() if W.ready then W:Send("destinations 0") end end)
button(pages[3],"Previous",210,350,100,function()
    if W.ready and W.travel and W.travel.page>0 then W:Send("destinations "..(W.travel.page-1)) end
end)
button(pages[3],"Next page",330,350,100,function()
    if W.ready and W.travel and W.travel.page+1<W.travel.pages then W:Send("destinations "..(W.travel.page+1)) end
end)
local characterText=label(pages[4], "", 0, 0, 760); characterText:SetHeight(170); characterText:SetJustifyV("TOP")
button(pages[4],"Queue training",0,350,180,function() W:Command("add",{4,1,1,1,0}) end,true)
button(pages[4], "Inspect equipment", 0, 185, 170, function() ToggleCharacter("PaperDollFrame") end)
button(pages[4], "Inspect talents", 180, 185, 150, function() ToggleTalentFrame() end)
button(pages[4], "Open bags", 340, 185, 130, function() OpenAllBags() end)
local policies={}
local classPolicies={}
for i,title in ipairs({"Spend earned talent points","Use class combat preset"}) do
    local index=i
    classPolicies[i]=button(pages[4],title,0,95+(i-1)*31,310,function()
        if not W.model then return end
        local p={unpack(W.model.classPolicy)}; p[index]=p[index]==1 and 0 or 1
        W:Command("classpolicy",p)
    end,true)
end
for i,title in ipairs({"Equip clear upgrades","Train class, weapon and riding skills","Allow bind-on-equip upgrades"}) do
    local index=i
    policies[i]=button(pages[4],title,0,230+(i-1)*32,310,function()
        if not W.model then return end
        local p={unpack(W.model.policy)}; p[index]=p[index]==1 and 0 or 1
        W:Command("policy",p)
    end,true)
end
label(pages[4],"Lock equipment slots",345,215,400,"GameFontNormalSmall")
local slotButtons={}
local slotNames={"Head","Neck","Shoulders","Shirt","Chest","Waist","Legs","Feet","Wrists","Hands",
    "Ring 1","Ring 2","Trinket 1","Trinket 2","Back","Main hand","Off hand","Ranged","Tabard",
    "Bag 1","Bag 2","Bag 3","Bag 4"}
for i,title in ipairs(slotNames) do
    local slot=i-1
    slotButtons[i]=button(pages[4],title,345+math.floor(slot/8)*140,238+(slot%8)*23,135,function()
        if not W.model then return end
        local p={unpack(W.model.policy)}; local weight=2^slot
        if math.floor(p[4]/weight)%2==1 then p[4]=p[4]-weight else p[4]=p[4]+weight end
        W:Command("policy",p)
    end,true)
end
local fields={}
local names={"Recover health %","Resume health %","Retreat health %","Free bag slots","Repair below %",
    "Gold reserve (copper)","Visit budget (copper)","Food stock","Food ID (0 = automatic)","Enemy min (0 = automatic)",
    "Enemy max (0 = automatic)","Search distance","Sell quality (0-2)"}
for i,title in ipairs(names) do
    local x,y=math.floor((i-1)/7)*390,((i-1)%7)*36
    label(pages[5], title, x, y+5, 265, "GameFontHighlightSmall")
    fields[i]=edit(pages[5],x+275,y,80,0)
end
button(pages[5], "Apply upkeep", 0, 270, 150, function()
    local args={}; for i,e in ipairs(fields) do args[i]=integer(e); if not args[i] then return end end
    W:Command("profile",args)
end,true)
label(pages[5], "Protected item IDs", 0, 312, 250)
local keep=edit(pages[5],5,338,530,"")
button(pages[5], "Protect items", 560, 338, 150, function()
    local ids=W.Numbers(keep:GetText()); if ids then W:Command("keep",ids) end
end,true)
local upgradeRows,upgradeButtons={},{}
label(pages[6],"Equipment upgrades awaiting a decision",0,0,760,"GameFontNormal")
for i=1,8 do
    local index=i
    upgradeRows[i]=label(pages[6],"",0,28+(i-1)*28,550,"GameFontHighlightSmall")
    upgradeButtons[i]=button(pages[6],"Equip",560,22+(i-1)*28,190,function()
        local item=W.upgrades and W.upgrades.items[index]
        if item then W:Command("equip",{item.guid}) end
    end,true)
end
button(pages[6],"Refresh upgrades",0,255,155,function() if W.ready then W:Send("upgrades 0") end end)
button(pages[6],"Previous",170,255,100,function()
    if W.ready and W.upgrades and W.upgrades.page>0 then W:Send("upgrades "..(W.upgrades.page-1)) end
end)
button(pages[6],"Next page",280,255,100,function()
    if W.ready and W.upgrades and W.upgrades.page+1<W.upgrades.pages then W:Send("upgrades "..(W.upgrades.page+1)) end
end)
label(pages[6],"Activity history",0,293,760,"GameFontNormalSmall")
local classDecision=label(pages[6],"",0,275,760,"GameFontNormalSmall")
local history=CreateFrame("ScrollingMessageFrame",nil,pages[6])
history:SetPoint("TOPLEFT",0,-315); history:SetSize(760,108); history:SetFontObject(GameFontHighlightSmall)
history:SetJustifyH("LEFT"); history:SetFading(false); history:SetMaxLines(200); history:EnableMouseWheel(true)
history:SetScript("OnMouseWheel",function(self,delta) if delta>0 then self:ScrollUp() else self:ScrollDown() end end)

function W:Paint(changed)
    activity:SetText("WrathIdle  |  "..self.action..(self.running and "  |  Running" or "  |  Paused"))
    reason:SetText(self.reason); errorText:SetText(self.error)
    vitals:SetText(UnitHealth("player").." HP  |  "..UnitPower("player").." / "..UnitPowerMax("player").." power")
    for _,b in ipairs(controls) do if self.ready and not self.pending then b:Enable() else b:Disable() end end
    if not self.model then return end
    for i,row in ipairs(rows) do
        local g=self.model.goals[i]
        row:SetText(g and ("#"..g[7].."  "..(g[1]==0 and ("Reach level "..g[2]) or
            (g[1]==2 and ("Complete quest "..g[2]) or (g[1]==4 and "Train class abilities" or ((g[1]==3 or g[1]==5) and
                (g[1]==3 and "Legacy travel: replace from Travel panel" or ("Travel to destination "..g[2])) or
                ("Collect "..g[3].." x "..(GetItemInfo(g[2]) or ("item "..g[2])))))))) or "")
    end
    scope:SetText("All Wrath races and classes, levels 1–80. Quests and local destinations come from world data.")
    if self.catalog then
        questText:SetText("Available and accepted quests  |  Page "..(self.catalog.page+1).." / "..self.catalog.pages)
    else questText:SetText("Refresh to browse available quests.") end
    for i,row in ipairs(questRows) do
        local q=self.catalog and self.catalog.quests[i]
        local avoided=false
        if q then for _,id in ipairs(self.model.avoid) do avoided=avoided or id==q.id end end
        row:SetText(q and (q.title.."  ["..q.id.."]  "..(avoided and "Avoided" or (q.supported and "" or "Requires an unsupported mechanic"))) or "")
    end
    travelText:SetText("Nearby quest givers, trainers and services  |  "..
        (self.travel and ("Page "..(self.travel.page+1).." / "..self.travel.pages) or "Refresh to browse destinations.")..
        "\nLocal walking only; other maps and transports require a supported route.")
    for i,row in ipairs(travelRows) do
        local destination=self.travel and self.travel.destinations[i]
        row:SetText(destination and destination.title or "")
    end
    characterText:SetText(UnitName("player").."  |  "..UnitRace("player").." "..UnitClass("player")..
        "\nLevel "..UnitLevel("player").."\n"..(self.className or "Reading class preset..."))
    classDecision:SetText(self.classDecision or "")
    for i,b in ipairs(classPolicies) do
        local names={"Spend earned talent points","Use class combat preset"}
        b:SetText(names[i]..(self.model.classPolicy[i]==1 and "  On" or "  Off"))
    end
    for i,b in ipairs(policies) do
        local names={"Equip clear upgrades","Train class, weapon and riding skills","Allow bind-on-equip upgrades"}
        b:SetText(names[i]..(self.model.policy[i]==1 and "  On" or "  Off"))
    end
    for i,b in ipairs(slotButtons) do
        b:SetText(slotNames[i]..(math.floor(self.model.policy[4]/2^(i-1))%2==1 and "  Locked" or ""))
    end
    if changed then
        for i,e in ipairs(fields) do e:SetText(self.model.profile[i]) end
        keep:SetText(table.concat(self.model.keep," "))
    end
    for i,row in ipairs(upgradeRows) do
        local item=self.upgrades and self.upgrades.items[i]
        row:SetText(item and item.title or "")
        upgradeButtons[i]:SetText(item and item.bind and "Equip and bind" or "Equip")
        if not item then upgradeButtons[i]:Disable() end
    end
    history:Clear(); for _,event in ipairs(self.events) do history:AddMessage(event) end
end

local events=CreateFrame("Frame")
events:RegisterEvent("PLAYER_ENTERING_WORLD"); events:RegisterEvent("UNIT_HEALTH")
local function hideActions()
    for _,name in ipairs({"MainMenuBar","MultiBarBottomLeft","MultiBarBottomRight","MultiBarLeft",
        "MultiBarRight","BonusActionBarFrame","ShapeshiftBarFrame","PetActionBarFrame","PossessBarFrame"}) do
        local f=_G[name]
        if f and not f.WrathIdleHidden then
            f.WrathIdleHidden=true; f:Hide()
            f:HookScript("OnShow",function(self) self:Hide() end)
        end
    end
end
events:SetScript("OnEvent",function(_,event)
    if event=="PLAYER_ENTERING_WORLD" then hideActions() end
    W:Refresh()
end)
SLASH_WRATHIDLE1="/wi"
SlashCmdList.WRATHIDLE=function() if main:IsShown() then main:Hide() else main:Show() end end
