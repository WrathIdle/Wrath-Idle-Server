local W = WrathIdle
local serial = 0
local function name()
    serial = serial + 1
    return "WrathIdleWidget" .. serial
end
local function text(parent, label, x, y, width, font)
    local t = parent:CreateFontString(nil, "OVERLAY", font or "GameFontHighlight")
    t:SetPoint("TOPLEFT", x, -y); t:SetWidth(width or 700); t:SetJustifyH("LEFT"); t:SetText(label)
    return t
end
local function button(parent, label, x, y, width, action, managed)
    local b = CreateFrame("Button", name(), parent, "UIPanelButtonTemplate")
    b:SetPoint("TOPLEFT", x, -y); b:SetSize(width, 24); b:SetText(label); b:SetScript("OnClick", action)
    if managed then W.controls[#W.controls + 1] = b end
    return b
end
local function edit(parent, x, y, width, value)
    local e = CreateFrame("EditBox", name(), parent, "InputBoxTemplate")
    e:SetPoint("TOPLEFT", x, -y); e:SetSize(width, 22); e:SetAutoFocus(false); e:SetMaxLetters(240)
    e:SetText(tostring(value or "")); e:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)
    e:SetScript("OnEnterPressed", function(self) self:ClearFocus() end)
    return e
end
local function number(e, maximum)
    local s = e:GetText()
    local n = s:match("^%d+$") and tonumber(s)
    if not n or n > (maximum or 10000000) then
        W.error = "Enter a whole number within the supported range."; W:Refresh(); return nil
    end
    return n
end
local function dropdown(parent, x, y, width, choices, value, changed)
    local d = CreateFrame("Frame", name(), parent, "UIDropDownMenuTemplate")
    d:SetPoint("TOPLEFT", x - 16, -y); UIDropDownMenu_SetWidth(d, width)
    d.choices = choices
    d.Select = function(self, v)
        self.value = v
        for _, choice in ipairs(self.choices) do
            if choice[1] == v then UIDropDownMenu_SetText(self, choice[2]); break end
        end
    end
    UIDropDownMenu_Initialize(d, function()
        for _, choice in ipairs(d.choices) do
            local id, label = choice[1], choice[2]
            local info = UIDropDownMenu_CreateInfo()
            info.text = label; info.checked = d.value == id
            info.func = function() d:Select(id); if changed then changed(id) end end
            UIDropDownMenu_AddButton(info)
        end
    end)
    d:Select(value)
    return d
end
local function check(parent, label, x, y)
    local c = CreateFrame("CheckButton", name(), parent, "UICheckButtonTemplate")
    c:SetPoint("TOPLEFT", x, -y); c:SetSize(26, 26)
    _G[c:GetName() .. "Text"]:SetText(label)
    return c
end
local function itemLabel(id)
    return GetItemInfo(id) or ("Item " .. id)
end

W.controls = {}
local frame = CreateFrame("Frame", "WrathIdleManager", UIParent)
W.frame = frame
frame:SetSize(780, 610); frame:SetPoint("CENTER"); frame:SetFrameStrata("DIALOG")
frame:SetClampedToScreen(true); frame:EnableMouse(true); frame:SetMovable(true)
frame:RegisterForDrag("LeftButton")
frame:SetScript("OnDragStart", frame.StartMoving); frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
frame:SetBackdrop({bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border", tile = true, tileSize = 32,
    edgeSize = 32, insets = {left = 11, right = 12, top = 12, bottom = 11}})
table.insert(UISpecialFrames, "WrathIdleManager")
local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", -5, -5)
text(frame, "WrathIdle", 24, 20, 200, "GameFontNormalLarge")
text(frame, "Human Warrior  |  Northshire pilot", 200, 23, 400, "GameFontNormalSmall")
local stateText = text(frame, "", 24, 49, 730, "GameFontNormal")
local reasonText = text(frame, "", 24, 69, 730)
reasonText:SetHeight(32); reasonText:SetJustifyV("TOP")
button(frame, "Start / Resume", 24, 108, 130, function() W:Command("resume") end, true)
button(frame, "Pause", 160, 108, 85, function() W:Command("pause") end, true)
button(frame, "Return to town", 251, 108, 130, function() W:Command("town") end, true)
button(frame, "Refresh", 665, 108, 90, function()
    W.ready = false; W.pending = nil; W.lastHello = -10; W:Refresh()
end)
local errorText = text(frame, "", 24, 139, 730, "GameFontRedSmall")
errorText:SetHeight(28); errorText:SetJustifyV("TOP")
local panels, tabs = {}, {}
local function showPanel(index)
    for i, panel in ipairs(panels) do
        if i == index then panel:Show(); tabs[i]:LockHighlight() else panel:Hide(); tabs[i]:UnlockHighlight() end
    end
end
for i, title in ipairs({"Goals", "Combat", "Supplies", "Results", "Roadmap"}) do
    local index = i
    tabs[i] = button(frame, title, 24 + (i - 1) * 146, 171, 140, function() showPanel(index) end)
    local panel = CreateFrame("Frame", nil, frame)
    panel:SetPoint("TOPLEFT", 24, -205); panel:SetSize(730, 375); panels[i] = panel
end

-- Goal queue: server-owned progress, editable outcomes, no direct action buttons.
local goals, rows = panels[1], {}
text(goals, "Goals run in order. Recovery and town visits temporarily interrupt them.", 0, 0, 730, "GameFontNormalSmall")
for i = 1, 8 do
    local index = i
    local row = CreateFrame("Frame", nil, goals); row:SetPoint("TOPLEFT", 0, -22 - (i - 1) * 29)
    row:SetSize(730, 28); rows[i] = row
    row.label = text(row, "", 0, 5, 525, "GameFontHighlightSmall")
    row.up = button(row, "Up", 535, 0, 50, function() W:Command("move", {index - 1, index - 2}) end, true)
    row.cancel = button(row, "Cancel", 595, 0, 95, function() W:Command("cancel", {index - 1}) end, true)
end
text(goals, "New goal", 0, 263, 150, "GameFontNormal")
local goalKind = dropdown(goals, 0, 283, 155, {{0, "Reach level"}, {1, "Collect item stock"}}, 0)
text(goals, "Level / item ID", 188, 265, 125, "GameFontNormalSmall")
local goalTarget = edit(goals, 193, 288, 90, 6)
text(goals, "Quantity", 310, 265, 85, "GameFontNormalSmall")
local goalCount = edit(goals, 315, 288, 70, 1)
text(goals, "Time limit (minutes)", 415, 265, 145, "GameFontNormalSmall")
local goalTime = edit(goals, 420, 288, 75, 0)
button(goals, "Add goal", 565, 287, 125, function()
    local target, count, minutes = number(goalTarget), number(goalCount, 10000), number(goalTime, 1440)
    if not target or not count or not minutes then return end
    if count < 1 or target < 1 or (goalKind.value == 0 and (target < 2 or target > 6)) then
        W.error = "Choose level 2-6, or an item ID and positive quantity."; W:Refresh(); return
    end
    W:Command("add", {goalKind.value, target, count, 1, minutes * 60})
end, true)
text(goals, "Area: Northshire. A time limit of 0 means no limit. Item goals count stock in your bags.", 0, 324, 710, "GameFontHighlightSmall")

-- Edit one priority slot at a time so applying a row never discards other drafts.
local combat, ruleLines = panels[2], {}
text(combat, "Ordered ability priorities", 0, 0, 700, "GameFontNormal")
for i = 1, 8 do ruleLines[i] = text(combat, "", 0, 25 + (i - 1) * 21, 710, "GameFontHighlightSmall") end
local selectedRule, ability, rage, health, selfTarget, missing
local function loadRule()
    if not W.model then return end
    local r = W.model.rules[selectedRule.value]
    ability:Select(r[1]); rage:SetText(r[2]); health:SetText(r[3]); selfTarget:SetChecked(r[4] == 1)
    missing:SetChecked(r[5] == 1)
end
local slots = {}; for i = 1, 8 do slots[i] = {i, "Priority " .. i} end
selectedRule = dropdown(combat, 0, 203, 130, slots, 1, loadRule)
local abilities = {{0, "Disabled"}, {6673, GetSpellInfo(6673) or "Battle Shout"},
    {78, GetSpellInfo(78) or "Heroic Strike"}, {772, GetSpellInfo(772) or "Rend"},
    {634, GetSpellInfo(634) or "Thunder Clap"}}
ability = dropdown(combat, 180, 203, 185, abilities, 0)
text(combat, "Minimum rage", 0, 247, 130, "GameFontNormalSmall"); rage = edit(combat, 5, 268, 75, 0)
text(combat, "Health at most (%)", 160, 247, 160, "GameFontNormalSmall"); health = edit(combat, 165, 268, 75, 100)
selfTarget = check(combat, "Target self", 325, 267); missing = check(combat, "Only when aura is missing", 460, 267)
button(combat, "Apply priority", 0, 307, 145, function()
    local r, h = number(rage, 100), number(health, 100)
    if r and h then W:Command("rule", {selectedRule.value - 1, ability.value, r, h,
        selfTarget:GetChecked() and 1 or 0, missing:GetChecked() and 1 or 0}) end
end, true)
text(combat, "Unlearned abilities, cooldowns, invalid targets, and insufficient rage are skipped. Normal melee attacks continue automatically.", 165, 309, 540, "GameFontHighlightSmall")

local supplies, fields = panels[3], {}
local labels = {"Recover below health %", "Resume at health %", "Retreat at health %", "Town at free bag slots",
    "Repair at durability %", "Gold reserve (copper)", "Town budget (copper)", "Food stock target",
    "Food: Darnassian Bleu (2070)", "Minimum enemy level", "Maximum enemy level", "Search distance (yards)",
    "Sell quality: 0 grey, 1 white, 2 green"}
for i, label in ipairs(labels) do
    local col, row = math.floor((i - 1) / 7), (i - 1) % 7
    local x, y = col * 365, row * 32
    text(supplies, label, x, y + 4, 260, "GameFontHighlightSmall")
    fields[i] = edit(supplies, x + 270, y, 65, 0)
    if i == 9 then fields[i]:EnableMouse(false) end
end
button(supplies, "Apply supplies and targets", 0, 231, 220, function()
    local values = {}
    for i = 1, 13 do values[i] = number(fields[i]); if not values[i] then return end end
    values[9] = 2070
    W:Command("profile", values)
end, true)
text(supplies, "Protected item IDs (spaces between IDs)", 0, 269, 700, "GameFontNormalSmall")
local keep = edit(supplies, 5, 292, 490)
button(supplies, "Apply protection", 520, 290, 180, function()
    local ids = W.Numbers(keep:GetText())
    if not ids or #ids > 32 then W.error = "Enter up to 32 item IDs, separated by spaces."; W:Refresh(); return end
    W:Command("keep", ids)
end, true)
text(supplies, "Equipped items, quest items, saved equipment sets, food, and queued item goals are always protected. Money is in copper: 100 copper = 1 silver.", 0, 326, 710, "GameFontHighlightSmall")

local results = panels[4]
local totals = text(results, "", 0, 0, 720, "GameFontHighlightSmall")
totals:SetHeight(80); totals:SetJustifyV("TOP")
text(results, "Activity history (last 200 decisions; mouse wheel to scroll)", 0, 84, 710, "GameFontNormalSmall")
local history = CreateFrame("ScrollingMessageFrame", nil, results)
history:SetPoint("TOPLEFT", 0, -108); history:SetSize(720, 240); history:SetFontObject(GameFontHighlightSmall)
history:SetJustifyH("LEFT"); history:SetFading(false); history:SetMaxLines(200); history:EnableMouseWheel(true)
history:SetScript("OnMouseWheel", function(self, delta) if delta > 0 then self:ScrollUp() else self:ScrollDown() end end)

text(panels[5], "Available now", 0, 0, 710, "GameFontNormal")
text(panels[5], "Human Warrior, levels 1-6\nNorthshire level and item goals\nCombat priorities, looting, rest, and corpse retrieval\nGoldshire selling, repair, and food policies", 0, 28, 710)
text(panels[5], "Later milestones", 0, 125, 710, "GameFontNormal")
text(panels[5], "Other classes and areas; equipment upgrades; training and talents; supported quests; professions and banking; parties and dungeons; mail, trade, and auctions.", 0, 153, 710)
text(panels[5], "Pause stops new actions and movement. The world continues, and enemies can still attack. Offline time gives no rewards. Spirit resurrection is not available in this pilot.", 0, 232, 710, "GameFontHighlightSmall")

function W:Paint(modelChanged)
    local enabled = self.ready and not self.pending
    for _, b in ipairs(self.controls) do if enabled then b:Enable() else b:Disable() end end
    stateText:SetText((self.running and "|cff40ff40" or "|cffffd100") .. self.action .. "|r")
    reasonText:SetText(self.reason)
    errorText:SetText(self.pending and "Waiting for server acknowledgement..." or self.error)
    if not self.ready and self.error == "" then errorText:SetText("Connecting to WrathIdle. Controls unlock when the server confirms your profile.") end
    local s = self.stats
    if self.model then
        for i, row in ipairs(rows) do
            local g = self.model.goals[i]
            if g then
                row:Show()
                local label = g[1] == 0 and ("Reach level " .. g[2]) or ("Collect " .. g[3] .. " x " .. itemLabel(g[2]))
                local progress = i == 1 and ("  [" .. (s[14] or 0) .. "/" .. (g[1] == 0 and g[2] or g[3]) .. "]") or ""
                row.label:SetText(i .. ". " .. label .. progress .. (g[5] > 0 and ("  |  " .. math.floor(g[5] / 60) .. " min limit") or ""))
                if i == 1 then row.up:Disable() end
            else row:Hide() end
        end
        if modelChanged then
            for i = 1, 13 do fields[i]:SetText(self.model.profile[i]) end
            keep:SetText(table.concat(self.model.keep, " "))
            loadRule()
            for i, r in ipairs(self.model.rules) do
                local spell = r[1] == 0 and "Disabled" or (GetSpellInfo(r[1]) or ("Spell " .. r[1]))
                ruleLines[i]:SetText(i .. ". " .. spell .. "  |  Rage >= " .. r[2] .. "  |  Health <= " .. r[3] .. "%  |  " ..
                    (r[4] == 1 and "Self" or "Enemy") .. (r[5] == 1 and "  |  Aura missing" or ""))
            end
        end
    else for _, row in ipairs(rows) do row:Hide() end end
    totals:SetText(string.format("Session: %d min   XP: %d   Earned: %dc   Spent: %dc\nDeaths: %d   Loot stacks: %d   Travel: %ds   Town service: %ds\nFree bag slots: %d   Durability: %d%%   Money: %dc   Food: %d",
        math.floor((s[1] or 0) / 60), s[2] or 0, s[3] or 0, s[4] or 0, s[5] or 0, s[6] or 0,
        s[7] or 0, s[8] or 0, s[9] or 0, s[10] or 0, s[11] or 0, s[12] or 0))
    if self.lastHistory ~= self.events[#self.events] then
        self.lastHistory = self.events[#self.events]; history:Clear()
        for _, line in ipairs(self.events) do history:AddMessage(line, 0.9, 0.9, 0.9) end
        history:ScrollToBottom()
    end
end

local toggle = button(UIParent, "WrathIdle", 20, 180, 105, function()
    if frame:IsShown() then frame:Hide() else frame:Show() end
end)
toggle:SetFrameStrata("MEDIUM")
SLASH_WRATHIDLE1 = "/wi"; SLASH_WRATHIDLE2 = "/wrathidle"
SlashCmdList.WRATHIDLE = function(command)
    if command == "pause" then W:Command("pause")
    elseif command == "resume" then W:Command("resume")
    elseif frame:IsShown() then frame:Hide() else frame:Show() end
end
showPanel(1); W:Paint()
