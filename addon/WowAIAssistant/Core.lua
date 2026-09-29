local addonName, ns = ...

local PREFIX = "|cff2bd9f7WoW AI Assistant|r"
local Core = CreateFrame("Frame")
ns.Core = Core

local function reportError(label, message)
    print(string.format("%s：%s 初始化失败；插件已隔离该错误。", PREFIX, label))
    if ns.Settings:Get().debugEnabled then
        print(string.format("%s 调试：%s", PREFIX, tostring(message)))
    end
end

function Core:Debug(message)
    if ns.Settings:Get().debugEnabled then
        print(string.format("%s 调试：%s", PREFIX, tostring(message)))
    end
end

function Core:SafeCall(label, callback, ...)
    local arguments = { ... }
    local argumentCount = select("#", ...)
    local function invoke()
        return callback(unpack(arguments, 1, argumentCount))
    end
    local function onError(message)
        reportError(label, message)
        return message
    end
    return xpcall(invoke, onError)
end

local function printHelp()
    print(PREFIX .. " 命令：")
    print("/wowai - 打开或关闭面板")
    print("/wowai show | hide | reset | status")
    print("/wowai scale <0.75-1.35>")
    print("/wowai debug on | off")
end

function Core:HandleSlashCommand(input)
    local command, argument = string.match(input or "", "^%s*(%S*)%s*(.-)%s*$")
    command = string.lower(command or "")

    if command == "" or command == "toggle" then
        ns.UI:TogglePanel()
    elseif command == "show" then
        ns.UI:ShowPanel()
    elseif command == "hide" then
        ns.UI:HidePanel()
    elseif command == "reset" then
        ns.Settings:ResetWindow()
        ns.UI:ApplySavedPlacement()
        ns.UI:SetScale(ns.Settings:Get().window.scale)
        print(PREFIX .. "：窗口位置、尺寸和缩放已恢复默认值。")
    elseif command == "scale" then
        local requested = tonumber(argument)
        if not requested then
            print(PREFIX .. "：缩放值必须是 0.75 到 1.35 之间的数字。")
            return
        end
        ns.UI:SetScale(requested)
        print(string.format("%s：当前缩放 %d%%。", PREFIX, ns.Settings:Get().window.scale * 100))
    elseif command == "debug" and (argument == "on" or argument == "off") then
        ns.Settings:SetDebugEnabled(argument == "on")
        print(PREFIX .. "：调试日志已" .. (argument == "on" and "开启。" or "关闭。"))
    elseif command == "status" then
        local status = ns.Context:GetPublicStatus()
        print(
            string.format(
                "%s：插件 %s；客户端 %s；伴侣程序 OFFLINE。",
                PREFIX,
                status.addonVersion,
                status.clientBuild
            )
        )
    else
        printHelp()
    end
end

function Core:Initialize()
    ns.Settings:Initialize()

    local initialized = self:SafeCall("UI", function()
        ns.UI:Initialize()
    end)
    if not initialized then
        return
    end

    SLASH_WOWAI1 = "/wowai"
    SlashCmdList.WOWAI = function(input)
        self:SafeCall("命令", function()
            self:HandleSlashCommand(input)
        end)
    end
    self:Debug("插件初始化完成，版本 " .. ns.Context:GetVersion())
end

Core:RegisterEvent("ADDON_LOADED")
Core:SetScript("OnEvent", function(_, event, loadedAddon)
    if event == "ADDON_LOADED" and loadedAddon == addonName then
        Core:UnregisterEvent("ADDON_LOADED")
        Core:SafeCall("核心", function()
            Core:Initialize()
        end)
    end
end)
