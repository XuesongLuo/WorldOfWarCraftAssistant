local addonName, ns = ...

ns.addonName = addonName
ns.Settings = ns.Settings or {}

local Settings = ns.Settings
local DEFAULTS = {
    schemaVersion = 2,
    window = {
        point = "CENTER",
        relativePoint = "CENTER",
        x = 0,
        y = 0,
        width = 560,
        height = 420,
        scale = 1,
    },
    panelShown = false,
    bridgeEnabled = false,
    bridgeFields = {
        class = true,
        specialization = true,
        level = true,
        zone = true,
        map = true,
        activity = true,
        encounter = true,
        achievement = true,
        criteria = true,
        events = true,
        skills = true,
        talents = true,
        actionSlots = true,
        keyBindings = true,
    },
    debugEnabled = false,
}

local VALID_POINTS = {
    BOTTOM = true,
    BOTTOMLEFT = true,
    BOTTOMRIGHT = true,
    CENTER = true,
    LEFT = true,
    RIGHT = true,
    TOP = true,
    TOPLEFT = true,
    TOPRIGHT = true,
}

local function clamp(value, minimum, maximum, fallback)
    if type(value) ~= "number" or value ~= value then
        return fallback
    end
    return math.max(minimum, math.min(maximum, value))
end

local function validPoint(value, fallback)
    if type(value) == "string" and VALID_POINTS[value] then
        return value
    end
    return fallback
end

function Settings:Initialize()
    local source = type(WowAIAssistantDB) == "table" and WowAIAssistantDB or {}
    local sourceWindow = type(source.window) == "table" and source.window or {}
    local sourceBridgeFields = type(source.bridgeFields) == "table" and source.bridgeFields or {}

    WowAIAssistantDB = {
        schemaVersion = DEFAULTS.schemaVersion,
        window = {
            point = validPoint(sourceWindow.point, DEFAULTS.window.point),
            relativePoint = validPoint(sourceWindow.relativePoint, DEFAULTS.window.relativePoint),
            x = clamp(sourceWindow.x, -10000, 10000, DEFAULTS.window.x),
            y = clamp(sourceWindow.y, -10000, 10000, DEFAULTS.window.y),
            width = clamp(sourceWindow.width, 420, 900, DEFAULTS.window.width),
            height = clamp(sourceWindow.height, 300, 700, DEFAULTS.window.height),
            scale = clamp(sourceWindow.scale, 0.75, 1.35, DEFAULTS.window.scale),
        },
        panelShown = source.panelShown == true,
        bridgeEnabled = source.bridgeEnabled == true,
        bridgeFields = {
            class = sourceBridgeFields.class ~= false,
            specialization = sourceBridgeFields.specialization ~= false,
            level = sourceBridgeFields.level ~= false,
            zone = sourceBridgeFields.zone ~= false,
            map = sourceBridgeFields.map ~= false,
            activity = sourceBridgeFields.activity ~= false,
            encounter = sourceBridgeFields.encounter ~= false,
            achievement = sourceBridgeFields.achievement ~= false,
            criteria = sourceBridgeFields.criteria ~= false,
            events = sourceBridgeFields.events ~= false,
            skills = sourceBridgeFields.skills ~= false,
            talents = sourceBridgeFields.talents ~= false,
            actionSlots = sourceBridgeFields.actionSlots ~= false,
            keyBindings = sourceBridgeFields.keyBindings ~= false,
        },
        debugEnabled = source.debugEnabled == true,
    }

    self.db = WowAIAssistantDB
    return self.db
end

function Settings:Get()
    return self.db or DEFAULTS
end

function Settings:SaveWindowPlacement(frame)
    if not self.db or not frame then
        return
    end

    local point, _, relativePoint, x, y = frame:GetPoint(1)
    self.db.window.point = validPoint(point, DEFAULTS.window.point)
    self.db.window.relativePoint = validPoint(relativePoint, DEFAULTS.window.relativePoint)
    self.db.window.x = clamp(x, -10000, 10000, DEFAULTS.window.x)
    self.db.window.y = clamp(y, -10000, 10000, DEFAULTS.window.y)
    self.db.window.width = clamp(frame:GetWidth(), 420, 900, DEFAULTS.window.width)
    self.db.window.height = clamp(frame:GetHeight(), 300, 700, DEFAULTS.window.height)
end

function Settings:SetScale(scale)
    if not self.db then
        return DEFAULTS.window.scale
    end
    self.db.window.scale = clamp(scale, 0.75, 1.35, DEFAULTS.window.scale)
    return self.db.window.scale
end

function Settings:SetPanelShown(shown)
    if self.db then
        self.db.panelShown = shown == true
    end
end

function Settings:SetDebugEnabled(enabled)
    if self.db then
        self.db.debugEnabled = enabled == true
    end
end

function Settings:SetBridgeEnabled(enabled)
    if self.db then
        self.db.bridgeEnabled = enabled == true
    end
end

function Settings:ResetWindow()
    if not self.db then
        return
    end
    for key, value in pairs(DEFAULTS.window) do
        self.db.window[key] = value
    end
end
