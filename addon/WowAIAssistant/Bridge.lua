local _, ns = ...

ns.Bridge = ns.Bridge or {}
local Bridge = ns.Bridge

Bridge.PROTOCOL_VERSION = "1"
Bridge.MAX_PAYLOAD_BYTES = 512
Bridge.COALESCE_SECONDS = 0.1
Bridge.SOURCE_PLUGIN_PUBLIC = 1

local FIELD_BITS = {
    class = 0x00000001, classId = 0x00000002, specialization = 0x00000004,
    specializationId = 0x00000008, level = 0x00000010, zone = 0x00000020,
    mapId = 0x00000040, activity = 0x00000080, encounterId = 0x00000100,
    achievementId = 0x00000200, criteria = 0x00000400, event = 0x00000800,
    skills = 0x00001000, talents = 0x00002000, actionSlots = 0x00004000,
    keyBindings = 0x00008000, unavailable = 0x80000000,
}

local PALETTE = {
    { 0.05, 0.05, 0.05, 1 }, { 0.8, 0.05, 0.05, 1 },
    { 0.05, 0.8, 0.05, 1 }, { 0.8, 0.8, 0.05, 1 },
    { 0.05, 0.05, 0.8, 1 }, { 0.8, 0.05, 0.8, 1 },
    { 0.05, 0.8, 0.8, 1 }, { 0.8, 0.8, 0.8, 1 },
    { 0.35, 0.35, 0.35, 1 }, { 1, 0.2, 0.2, 1 },
    { 0.2, 1, 0.2, 1 }, { 1, 1, 0.2, 1 },
    { 0.2, 0.2, 1, 1 }, { 1, 0.2, 1, 1 },
    { 0.2, 1, 1, 1 }, { 1, 1, 1, 1 },
}

local function percentEncode(value)
    return string.gsub(value, "([^%w%-%._~])", function(character)
        return string.format("%%%02X", string.byte(character))
    end)
end

local function serialize(snapshot)
    local fields = {}
    for key, value in pairs(snapshot) do
        local valueType = type(value)
        if valueType == "string" then
            table.insert(fields, { key = key, value = key .. "=" .. percentEncode(value) })
        elseif valueType == "number" then
            table.insert(fields, { key = key, value = key .. "=" .. string.format("%d", value) })
        end
    end
    table.sort(fields, function(left, right)
        return left.key < right.key
    end)
    local accepted = {}
    local transmitted = {}
    local omitted = {}
    local bitmap = 0
    for _, field in ipairs(fields) do
        local separator = #accepted == 0 and 0 or 1
        if FIELD_BITS[field.key] and #table.concat(accepted, "&") + separator + #field.value <= Bridge.MAX_PAYLOAD_BYTES then
            table.insert(accepted, field.value)
            bitmap = bit.bor(bitmap, FIELD_BITS[field.key])
            transmitted[field.key] = snapshot[field.key]
        elseif FIELD_BITS[field.key] then
            table.insert(omitted, field.key)
        end
    end
    if #omitted > 0 then
        transmitted.truncated = table.concat(omitted, ",")
    end
    return table.concat(accepted, "&"), bitmap, transmitted
end

local function crc32(text)
    local value = 0xFFFFFFFF
    for index = 1, #text do
        value = bit.bxor(value, string.byte(text, index))
        for _ = 1, 8 do
            local mask = -bit.band(value, 1)
            value = bit.bxor(bit.rshift(value, 1), bit.band(0xEDB88320, mask))
        end
    end
    return bit.bnot(value)
end

local function packU16(value)
    return string.char(bit.band(bit.rshift(value, 8), 0xFF), bit.band(value, 0xFF))
end

local function packU32(value)
    return string.char(
        bit.band(bit.rshift(value, 24), 0xFF),
        bit.band(bit.rshift(value, 16), 0xFF),
        bit.band(bit.rshift(value, 8), 0xFF),
        bit.band(value, 0xFF)
    )
end

function Bridge:Initialize(parent)
    if self.frame then
        return
    end
    self.sequence = 0
    self.cells = {}
    self.frame = CreateFrame("Frame", "WowAIAssistantVisibleDataBridge", parent)
    self.frame:SetSize(192, 54)
    self.frame:SetPoint("BOTTOMLEFT", parent, "BOTTOMLEFT", 16, 18)
    self.frame:Hide()

    self.eventFrame = CreateFrame("Frame")
    for _, event in ipairs({
        "PLAYER_ENTERING_WORLD",
        "PLAYER_LEVEL_UP",
        "PLAYER_SPECIALIZATION_CHANGED",
        "ZONE_CHANGED_NEW_AREA",
        "TRAIT_CONFIG_UPDATED",
        "SPELLS_CHANGED",
        "ACTIONBAR_SLOT_CHANGED",
        "UPDATE_BINDINGS",
        "ENCOUNTER_START",
        "ENCOUNTER_END",
        "TRACKED_ACHIEVEMENT_UPDATE",
        "TRACKED_ACHIEVEMENT_LIST_CHANGED",
    }) do
        self.eventFrame:RegisterEvent(event)
    end
    self.eventFrame:SetScript("OnEvent", function(_, event, ...)
        if event == "ENCOUNTER_START" then
            ns.Context:SetEncounter(select(1, ...))
        elseif event == "ENCOUNTER_END" then
            ns.Context:SetEncounter(nil)
        end
        if event == "PLAYER_LEVEL_UP" or event == "PLAYER_SPECIALIZATION_CHANGED" or
            event == "SPELLS_CHANGED" or event == "TRAIT_CONFIG_UPDATED" or
            event == "ACTIONBAR_SLOT_CHANGED" or event == "UPDATE_BINDINGS" or
            event == "TRACKED_ACHIEVEMENT_UPDATE" or event == "TRACKED_ACHIEVEMENT_LIST_CHANGED" then
            ns.Context:SetLastPlayerEvent(event)
        end
        self:RequestPublish()
    end)
end

function Bridge:IsEnabled()
    return ns.Settings:Get().bridgeEnabled == true
end

function Bridge:SetEnabled(enabled)
    ns.Settings:SetBridgeEnabled(enabled)
    if enabled then
        self.frame:Show()
        self:RequestPublish()
    else
        self.frame:Hide()
    end
end

function Bridge:RequestPublish()
    if not self:IsEnabled() or self.publishPending then
        return
    end
    self.publishPending = true
    C_Timer.After(self.COALESCE_SECONDS, function()
        self.publishPending = false
        if self:IsEnabled() then
            self:Publish()
        end
    end)
end

function Bridge:Publish()
    local snapshot = ns.Context:GetPublicSnapshot()
    local payload, fieldBitmap, transmitted = serialize(snapshot)
    self.sequence = bit.band(self.sequence + 1, 0xFFFFFFFF)
    local capturedAt = time and time() or 0
    local body = "WAI" .. string.char(tonumber(self.PROTOCOL_VERSION)) ..
        string.char(self.SOURCE_PLUGIN_PUBLIC) .. packU32(self.sequence) ..
        packU32(capturedAt) .. packU32(fieldBitmap) .. packU16(#payload) .. payload
    local frame = body .. packU32(crc32(body))
    self:Render(frame)
    if ns.UI and ns.UI.UpdateBridgePreview then
        ns.UI:UpdateBridgePreview(transmitted, self.sequence, #payload)
    end
end

function Bridge:Render(frame)
    local nibbleCount = #frame * 2
    for index = 1, nibbleCount do
        local cell = self.cells[index]
        if not cell then
            cell = self.frame:CreateTexture(nil, "OVERLAY")
            cell:SetSize(3, 3)
            local zeroBased = index - 1
            cell:SetPoint("TOPLEFT", self.frame, "TOPLEFT", (zeroBased % 64) * 3, -math.floor(zeroBased / 64) * 3)
            self.cells[index] = cell
        end
        local byte = string.byte(frame, math.floor((index + 1) / 2))
        local nibble = index % 2 == 1 and bit.rshift(byte, 4) or bit.band(byte, 0x0F)
        cell:SetColorTexture(unpack(PALETTE[nibble + 1]))
        cell:Show()
    end
    for index = nibbleCount + 1, #self.cells do
        self.cells[index]:Hide()
    end
end
