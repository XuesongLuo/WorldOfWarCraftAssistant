local _, ns = ...

ns.UI = ns.UI or {}
local UI = ns.UI

local COLORS = {
    background = { 0.035, 0.047, 0.071, 0.97 },
    border = { 0.18, 0.67, 0.82, 1 },
    muted = { 0.62, 0.69, 0.77, 1 },
    offline = { 0.96, 0.52, 0.24, 1 },
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

local function createAnchor(parent)
    local anchor = CreateFrame("Frame", "WowAIAssistantAnchor", parent)
    anchor:SetSize(20, 20)
    anchor:SetPoint("TOPLEFT", parent, "TOPLEFT", 7, -7)
    anchor:SetFrameLevel(parent:GetFrameLevel() + 5)

    local colors = {
        { 0, 1, 1, 1 },
        { 1, 0, 1, 1 },
        { 1, 1, 1, 1 },
        { 0, 0.12, 0.18, 1 },
    }
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

function UI:HandleSend()
    if not self.input or self.input:GetText() == "" then
        self:SetNotice("请输入问题；当前不会向外部服务发送任何内容。")
        self.input:SetFocus()
        return
    end
    self:SetNotice("伴侣程序未运行。问题已保留，请启动伴侣程序后重试。")
end

function UI:SetNotice(message)
    self.notice:SetText(message)
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

    self.anchor = createAnchor(frame)

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

    local statusDot = status:CreateTexture(nil, "OVERLAY")
    statusDot:SetColorTexture(unpack(COLORS.offline))
    statusDot:SetSize(8, 8)
    statusDot:SetPoint("LEFT", status, "LEFT", 12, 0)

    local statusText = createText(status, "GameFontNormalSmall", "伴侣程序：离线")
    statusText:SetPoint("LEFT", statusDot, "RIGHT", 7, 0)
    statusText:SetTextColor(unpack(COLORS.offline))

    local messagePanel = CreateFrame("Frame", nil, frame, "BackdropTemplate")
    messagePanel:SetPoint("TOPLEFT", status, "BOTTOMLEFT", 0, -8)
    messagePanel:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -12, 94)
    setBackdrop(messagePanel, COLORS.panel, { 0.13, 0.2, 0.27, 1 })

    local messageTitle = createText(messagePanel, "GameFontNormal", "消息")
    messageTitle:SetPoint("TOPLEFT", messagePanel, "TOPLEFT", 12, -10)
    messageTitle:SetTextColor(unpack(COLORS.text))

    local placeholder = createText(
        messagePanel,
        "GameFontHighlight",
        "助手内容将显示在这里。\n\nSTEP-005 仅提供安全的界面占位；插件不会联网，也不会控制游戏。"
    )
    placeholder:SetPoint("TOPLEFT", messageTitle, "BOTTOMLEFT", 0, -14)
    placeholder:SetPoint("RIGHT", messagePanel, "RIGHT", -12, 0)
    placeholder:SetJustifyH("LEFT")
    placeholder:SetJustifyV("TOP")
    placeholder:SetTextColor(unpack(COLORS.muted))

    local input = CreateFrame("EditBox", "WowAIAssistantInput", frame, "BackdropTemplate")
    self.input = input
    input:SetPoint("BOTTOMLEFT", frame, "BOTTOMLEFT", 12, 50)
    input:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -90, 50)
    input:SetHeight(38)
    input:SetAutoFocus(false)
    input:SetFontObject("ChatFontNormal")
    input:SetTextInsets(10, 10, 8, 8)
    input:SetMaxLetters(2000)
    setBackdrop(input, { 0.025, 0.035, 0.055, 1 }, { 0.18, 0.28, 0.36, 1 })
    input:SetScript("OnEnterPressed", function()
        self:HandleSend()
    end)
    input:SetScript("OnEscapePressed", function(current)
        current:ClearFocus()
    end)

    local send = createButton(frame, "发送", 70, function()
        self:HandleSend()
    end)
    send:SetPoint("LEFT", input, "RIGHT", 8, 0)
    send:SetHeight(38)

    self.notice = createText(frame, "GameFontHighlightSmall", "伴侣程序未运行；插件仍可独立使用。")
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
    if ns.Settings:Get().panelShown then
        frame:Show()
    else
        frame:Hide()
    end
end
