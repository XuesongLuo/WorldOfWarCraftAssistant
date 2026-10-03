local _, ns = ...

ns.UI = ns.UI or {}
local UI = ns.UI

local COLORS = {
    background = { 0.035, 0.047, 0.071, 0.97 },
    border = { 0.18, 0.67, 0.82, 1 },
    muted = { 0.62, 0.69, 0.77, 1 },
    active = { 0.26, 0.86, 0.55, 1 },
    panel = { 0.055, 0.075, 0.11, 0.94 },
    text = { 0.92, 0.95, 0.98, 1 },
}

local BACKDROP = {
    bgFile = "Interface\\Buttons\\WHITE8X8",
    edgeFile = "Interface\\Buttons\\WHITE8X8",
    edgeSize = 1,
}

local function setBackdrop(frame, color, border)
    frame:SetBackdrop(BACKDROP)
    frame:SetBackdropColor(unpack(color))
    frame:SetBackdropBorderColor(unpack(border or COLORS.border))
end

local function createText(parent, template, text)
    local label = parent:CreateFontString(nil, "OVERLAY", template)
    label:SetText(text)
    return label
end

local function createAnchor(parent, name, point, x, y, colors)
    local anchor = CreateFrame("Frame", name, parent)
    anchor:SetSize(20, 20)
    anchor:SetPoint(point, parent, point, x, y)
    anchor:SetFrameLevel(parent:GetFrameLevel() + 5)

    local points = {
        { "TOPLEFT", 0, 0 },
        { "TOPRIGHT", 0, 0 },
        { "BOTTOMLEFT", 0, 0 },
        { "BOTTOMRIGHT", 0, 0 },
    }
    for index = 1, 4 do
        local texture = anchor:CreateTexture(nil, "OVERLAY")
        texture:SetColorTexture(unpack(colors[index]))
        texture:SetSize(10, 10)
        texture:SetPoint(
            points[index][1],
            anchor,
            points[index][1],
            points[index][2],
            points[index][3]
        )
    end
    return anchor
end

local function createButton(parent, text, width, onClick)
    local button = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
    button:SetSize(width, 24)
    button:SetText(text)
    button:SetScript("OnClick", onClick)
    return button
end

function UI:ApplySavedPlacement()
    local settings = ns.Settings:Get().window
    self.frame:ClearAllPoints()
    self.frame:SetPoint(settings.point, UIParent, settings.relativePoint, settings.x, settings.y)
    self.frame:SetSize(settings.width, settings.height)
    self.frame:SetScale(settings.scale)
end

function UI:SetScale(scale)
    local centerX, centerY = self.frame:GetCenter()
    local oldFrameScale = self.frame:GetEffectiveScale()
    local screenX = centerX and centerX * oldFrameScale
    local screenY = centerY and centerY * oldFrameScale

    local savedScale = ns.Settings:SetScale(scale)
    self.frame:SetScale(savedScale)

    if screenX and screenY then
        local newFrameScale = self.frame:GetEffectiveScale()
        self.frame:ClearAllPoints()
        self.frame:SetPoint(
            "CENTER",
            UIParent,
            "BOTTOMLEFT",
            screenX / newFrameScale,
            screenY / newFrameScale
        )
    end

    self.scaleText:SetFormattedText("%d%%", math.floor(savedScale * 100 + 0.5))
    ns.Settings:SaveWindowPlacement(self.frame)
end

function UI:SetNotice(message)
    self.notice:SetText(message)
end

function UI:RefreshBridgeState()
    local enabled = ns.Bridge:IsEnabled()
    self.bridgeButton:SetText(enabled and "关闭数据桥" or "开启数据桥")
    self.bridgeStatus:SetText(enabled and "数据桥：已开启并持续可见" or "数据桥：已关闭")
    self.bridgeStatus:SetTextColor(unpack(enabled and COLORS.active or COLORS.muted))
    self:SetNotice(enabled and "仅发布下方预览中的公开字段；伴侣程序没有返回通道。" or
        "插件是可选上下文源；聊天请使用 Windows 覆盖层。")
end

function UI:UpdateBridgePreview(snapshot, sequence, payloadBytes)
    local values = {}
    for _, key in ipairs({ "class", "specialization", "level", "zone", "mapId", "activity",
        "encounterId", "achievementId", "criteria", "event", "skills", "talents",
        "actionSlots", "keyBindings", "unavailable", "truncated" }) do
        if snapshot[key] ~= nil then
            table.insert(values, key .. ": " .. tostring(snapshot[key]))
        end
    end
    self.preview:SetText(table.concat(values, "\n"))
    self.bridgeMeta:SetFormattedText("协议 v%s · 帧 %d · %d / %d 字节", ns.Bridge.PROTOCOL_VERSION,
        sequence, payloadBytes, ns.Bridge.MAX_PAYLOAD_BYTES)
end

function UI:ShowPanel()
    self.frame:Show()
    ns.Settings:SetPanelShown(true)
end

function UI:HidePanel()
    self.frame:Hide()
    ns.Settings:SetPanelShown(false)
end

function UI:TogglePanel()
    if self.frame:IsShown() then
        self:HidePanel()
    else
        self:ShowPanel()
    end
end

function UI:Initialize()
    if self.frame then
        return
    end

    local frame = CreateFrame("Frame", "WowAIAssistantFrame", UIParent, "BackdropTemplate")
    self.frame = frame
    frame:SetFrameStrata("DIALOG")
    frame:SetMovable(true)
    frame:SetResizable(true)
    frame:SetResizeBounds(420, 300, 900, 700)
    frame:SetClampedToScreen(true)
    frame:EnableMouse(true)
    setBackdrop(frame, COLORS.background)
    self:ApplySavedPlacement()

    local titleBar = CreateFrame("Frame", "WowAIAssistantTitleBar", frame)
    titleBar:SetPoint("TOPLEFT", frame, "TOPLEFT", 0, 0)
    titleBar:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -156, 0)
    titleBar:SetHeight(40)
    titleBar:EnableMouse(true)
    titleBar:RegisterForDrag("LeftButton")
    titleBar:SetScript("OnDragStart", function()
        frame:StartMoving()
    end)
    titleBar:SetScript("OnDragStop", function()
        frame:StopMovingOrSizing()
        ns.Settings:SaveWindowPlacement(frame)
    end)
    frame:SetScript("OnHide", function()
        ns.Settings:SetPanelShown(false)
    end)

    self.topLeftAnchor = createAnchor(frame, "WowAIAssistantAnchor", "TOPLEFT", 7, -7, {
        { 0, 1, 1, 1 },
        { 1, 0, 1, 1 },
        { 1, 1, 1, 1 },
        { 0, 0.12, 0.18, 1 },
    })
    self.bottomRightAnchor = createAnchor(
        frame,
        "WowAIAssistantBottomRightAnchor",
        "BOTTOMRIGHT",
        -31,
        7,
        {
            { 1, 0, 1, 1 },
            { 0, 1, 1, 1 },
            { 0, 0.12, 0.18, 1 },
            { 1, 1, 1, 1 },
        }
    )

    ns.Bridge:Initialize(frame)

    local title = createText(frame, "GameFontNormalLarge", "WoW AI Assistant")
    title:SetPoint("TOPLEFT", frame, "TOPLEFT", 34, -13)
    title:SetTextColor(unpack(COLORS.text))

    local version = createText(frame, "GameFontHighlightSmall", "v" .. ns.Context:GetVersion())
    version:SetPoint("LEFT", title, "RIGHT", 8, 0)
    version:SetTextColor(unpack(COLORS.muted))

    local close = createButton(frame, "X", 28, function()
        self:HidePanel()
    end)
    close:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -10, -10)

    local scaleUp = createButton(frame, "+", 28, function()
        self:SetScale(ns.Settings:Get().window.scale + 0.05)
    end)
    scaleUp:SetPoint("RIGHT", close, "LEFT", -6, 0)

    self.scaleText = createText(frame, "GameFontHighlightSmall", "100%")
    self.scaleText:SetWidth(44)
    self.scaleText:SetPoint("RIGHT", scaleUp, "LEFT", -3, 0)
    self.scaleText:SetTextColor(unpack(COLORS.muted))

    local scaleDown = createButton(frame, "-", 28, function()
        self:SetScale(ns.Settings:Get().window.scale - 0.05)
    end)
    scaleDown:SetPoint("RIGHT", self.scaleText, "LEFT", -3, 0)

    local status = CreateFrame("Frame", nil, frame, "BackdropTemplate")
    status:SetPoint("TOPLEFT", frame, "TOPLEFT", 12, -44)
    status:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -12, -44)
    status:SetHeight(34)
    setBackdrop(status, COLORS.panel, { 0.18, 0.24, 0.31, 1 })

    self.bridgeStatus = createText(status, "GameFontNormalSmall", "数据桥：已关闭")
    self.bridgeStatus:SetPoint("LEFT", status, "LEFT", 12, 0)

    local messagePanel = CreateFrame("Frame", nil, frame, "BackdropTemplate")
    messagePanel:SetPoint("TOPLEFT", status, "BOTTOMLEFT", 0, -8)
    messagePanel:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -12, 92)
    setBackdrop(messagePanel, COLORS.panel, { 0.13, 0.2, 0.27, 1 })

    local messageTitle = createText(messagePanel, "GameFontNormal", "公开上下文预览")
    messageTitle:SetPoint("TOPLEFT", messagePanel, "TOPLEFT", 12, -10)
    messageTitle:SetTextColor(unpack(COLORS.text))

    self.preview = createText(
        messagePanel,
        "GameFontHighlight",
        "尚未发布字段。开启数据桥后，这里会显示伴侣程序可读取的全部结构化内容。"
    )
    self.preview:SetPoint("TOPLEFT", messageTitle, "BOTTOMLEFT", 0, -14)
    self.preview:SetPoint("RIGHT", messagePanel, "RIGHT", -12, 0)
    self.preview:SetJustifyH("LEFT")
    self.preview:SetJustifyV("TOP")
    self.preview:SetTextColor(unpack(COLORS.muted))

    self.bridgeMeta = createText(messagePanel, "GameFontHighlightSmall", "协议 v1 · 尚无帧")
    self.bridgeMeta:SetPoint("BOTTOMLEFT", messagePanel, "BOTTOMLEFT", 12, 10)
    self.bridgeMeta:SetTextColor(unpack(COLORS.muted))

    self.bridgeButton = createButton(frame, "开启数据桥", 108, function()
        ns.Bridge:SetEnabled(not ns.Bridge:IsEnabled())
        self:RefreshBridgeState()
    end)
    self.bridgeButton:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -32, 50)
    self.bridgeButton:SetHeight(30)

    self.notice = createText(frame, "GameFontHighlightSmall", "插件是可选上下文源；聊天请使用 Windows 覆盖层。")
    self.notice:SetPoint("BOTTOMLEFT", frame, "BOTTOMLEFT", 14, 10)
    self.notice:SetPoint("RIGHT", frame, "RIGHT", -36, 0)
    self.notice:SetHeight(32)
    self.notice:SetJustifyH("LEFT")
    self.notice:SetJustifyV("MIDDLE")
    self.notice:SetTextColor(unpack(COLORS.muted))

    local resize = CreateFrame("Button", nil, frame)
    resize:SetSize(20, 20)
    resize:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -4, 4)
    local resizeTexture = resize:CreateTexture(nil, "OVERLAY")
    resizeTexture:SetAllPoints()
    resizeTexture:SetTexture("Interface\\ChatFrame\\UI-ChatIM-SizeGrabber-Up")
    resize:SetScript("OnMouseDown", function(_, button)
        if button == "LeftButton" then
            frame:StartSizing("BOTTOMRIGHT")
        end
    end)
    resize:SetScript("OnMouseUp", function()
        frame:StopMovingOrSizing()
        ns.Settings:SaveWindowPlacement(frame)
    end)

    self:SetScale(ns.Settings:Get().window.scale)
    ns.Bridge:SetEnabled(ns.Settings:Get().bridgeEnabled)
    self:RefreshBridgeState()
    if ns.Settings:Get().panelShown then
        frame:Show()
    else
        frame:Hide()
    end
end
