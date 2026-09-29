local _, ns = ...

ns.Context = ns.Context or {}
local Context = ns.Context

local function getAddonMetadata(field)
    if C_AddOns and C_AddOns.GetAddOnMetadata then
        return C_AddOns.GetAddOnMetadata(ns.addonName, field)
    end
    if GetAddOnMetadata then
        return GetAddOnMetadata(ns.addonName, field)
    end
    return nil
end

function Context:GetVersion()
    return getAddonMetadata("Version") or "unknown"
end

function Context:GetClientBuild()
    if not GetBuildInfo then
        return "unknown"
    end
    local version, build = GetBuildInfo()
    if type(version) ~= "string" or type(build) ~= "string" then
        return "unknown"
    end
    return version .. "." .. build
end

function Context:GetPublicStatus()
    return {
        addonVersion = self:GetVersion(),
        clientBuild = self:GetClientBuild(),
        companion = "OFFLINE",
    }
end
