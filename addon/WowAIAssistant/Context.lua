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
        bridgeEnabled = ns.Settings:Get().bridgeEnabled == true,
        bridgeProtocol = ns.Bridge and ns.Bridge.PROTOCOL_VERSION or "unavailable",
    }
end

local function isExportable(value, expectedType)
    if type(value) ~= expectedType then
        return false
    end
    if issecretvalue and issecretvalue(value) then
        return false
    end
    return true
end

function Context:GetPublicSnapshot()
    local snapshot = {}
    local fields = ns.Settings:Get().bridgeFields or {}

    if fields.class then
        local _, classFile, classId = UnitClass("player")
        if isExportable(classFile, "string") and isExportable(classId, "number") then
            snapshot.class = classFile
            snapshot.classId = math.floor(classId)
        end
    end

    if fields.specialization and GetSpecialization and GetSpecializationInfo then
        local specializationIndex = GetSpecialization()
        if isExportable(specializationIndex, "number") then
            local specializationId, specializationName = GetSpecializationInfo(specializationIndex)
            if isExportable(specializationId, "number") then
                snapshot.specializationId = math.floor(specializationId)
            end
            if isExportable(specializationName, "string") then
                snapshot.specialization = specializationName
            end
        end
    end

    if fields.level then
        local level = UnitLevel("player")
        if isExportable(level, "number") then
            snapshot.level = math.floor(level)
        end
    end

    if fields.zone and GetRealZoneText then
        local zone = GetRealZoneText()
        if isExportable(zone, "string") then
            snapshot.zone = zone
        end
    end

    if fields.map and C_Map and C_Map.GetBestMapForUnit then
        local mapId = C_Map.GetBestMapForUnit("player")
        if isExportable(mapId, "number") then
            snapshot.mapId = math.floor(mapId)
        end
    end

    return snapshot
end
