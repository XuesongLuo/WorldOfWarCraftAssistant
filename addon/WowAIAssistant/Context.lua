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
    if issecretvalue and issecretvalue(value) then
        return false
    end
    return type(value) == expectedType
end

local function isPlainTable(value)
    if issecretvalue and issecretvalue(value) then
        return false
    end
    return type(value) == "table"
end

Context.FIELD_AUDIT = {
    class = { source = "UnitClass", valueType = "string", mayBeSecret = false, purpose = "build", since = 1 },
    classId = { source = "UnitClass", valueType = "number", mayBeSecret = false, purpose = "build", since = 1 },
    specialization = { source = "GetSpecializationInfo", valueType = "string", mayBeSecret = false, purpose = "build", since = 1 },
    specializationId = { source = "GetSpecializationInfo", valueType = "number", mayBeSecret = false, purpose = "build", since = 1 },
    level = { source = "UnitLevel", valueType = "number", mayBeSecret = true, purpose = "build", since = 1 },
    zone = { source = "GetRealZoneText", valueType = "string", mayBeSecret = true, purpose = "game-state", since = 1 },
    mapId = { source = "C_Map.GetBestMapForUnit", valueType = "number", mayBeSecret = true, purpose = "game-state", since = 1 },
    activity = { source = "IsInInstance", valueType = "string", mayBeSecret = true, purpose = "game-state", since = 1 },
    encounterId = { source = "ENCOUNTER_START/END", valueType = "number", mayBeSecret = true, purpose = "achievement-progress", since = 1 },
    achievementId = { source = "GetTrackedAchievements", valueType = "string", mayBeSecret = true, purpose = "achievement-progress", since = 1 },
    criteria = { source = "GetAchievementCriteriaInfo", valueType = "string", mayBeSecret = true, purpose = "achievement-progress", since = 1 },
    event = { source = "allowlisted player events", valueType = "string", mayBeSecret = true, purpose = "game-state", since = 1 },
    skills = { source = "C_SpellBook", valueType = "string", mayBeSecret = true, purpose = "build", since = 1 },
    talents = { source = "C_ClassTalents/C_Traits", valueType = "string", mayBeSecret = true, purpose = "build", since = 1 },
    actionSlots = { source = "GetActionInfo", valueType = "string", mayBeSecret = true, purpose = "build", since = 1 },
    keyBindings = { source = "GetBindingKey", valueType = "string", mayBeSecret = true, purpose = "build", since = 1 },
}

local function appendPlainNumber(target, value)
    if isExportable(value, "number") then
        table.insert(target, tostring(math.floor(value)))
        return true
    end
    return false
end

local function collectSkills()
    if not C_SpellBook or not C_SpellBook.GetNumSpellBookSkillLines or
        not C_SpellBook.GetSpellBookSkillLineInfo or not C_SpellBook.GetSpellBookItemInfo or
        not Enum or not Enum.SpellBookSpellBank then
        return nil
    end
    local result = {}
    local lineCount = C_SpellBook.GetNumSpellBookSkillLines()
    if not isExportable(lineCount, "number") then
        return nil
    end
    for lineIndex = 1, math.min(math.floor(lineCount), 32) do
        local line = C_SpellBook.GetSpellBookSkillLineInfo(lineIndex)
        if isPlainTable(line) and isExportable(line.itemIndexOffset, "number") and
            isExportable(line.numSpellBookItems, "number") then
            local first = math.floor(line.itemIndexOffset) + 1
            local last = math.min(first + math.floor(line.numSpellBookItems) - 1, first + 127)
            for slot = first, last do
                local item = C_SpellBook.GetSpellBookItemInfo(slot, Enum.SpellBookSpellBank.Player)
                if isPlainTable(item) and appendPlainNumber(result, item.spellID) and #result >= 48 then
                    return table.concat(result, ",")
                end
            end
        end
    end
    return #result > 0 and table.concat(result, ",") or nil
end

local function collectTalents()
    if not C_ClassTalents or not C_ClassTalents.GetActiveConfigID or not C_Traits or
        not C_Traits.GetConfigInfo or not C_Traits.GetTreeNodes or not C_Traits.GetNodeInfo then
        return nil
    end
    local configId = C_ClassTalents.GetActiveConfigID()
    if not isExportable(configId, "number") then
        return nil
    end
    local config = C_Traits.GetConfigInfo(configId)
    if not isPlainTable(config) or not isPlainTable(config.treeIDs) then
        return nil
    end
    local result = {}
    for _, treeId in ipairs(config.treeIDs) do
        if isExportable(treeId, "number") then
            local nodes = C_Traits.GetTreeNodes(treeId)
            if isPlainTable(nodes) then
                for _, nodeId in ipairs(nodes) do
                    if isExportable(nodeId, "number") then
                        local node = C_Traits.GetNodeInfo(configId, nodeId)
                        if isPlainTable(node) and isExportable(node.ranksPurchased, "number") and
                            node.ranksPurchased > 0 then
                            appendPlainNumber(result, nodeId)
                            if #result >= 48 then
                                return table.concat(result, ",")
                            end
                        end
                    end
                end
            end
        end
    end
    return #result > 0 and table.concat(result, ",") or nil
end

local function collectActions()
    if not GetActionInfo then
        return nil
    end
    local result = {}
    for slot = 1, 180 do
        local actionType, id = GetActionInfo(slot)
        if isExportable(actionType, "string") and isExportable(id, "number") then
            table.insert(result, slot .. ":" .. actionType .. ":" .. math.floor(id))
            if #result >= 36 then
                break
            end
        end
    end
    return #result > 0 and table.concat(result, ",") or nil
end

local BINDING_ACTIONS = {
    "ACTIONBUTTON1", "ACTIONBUTTON2", "ACTIONBUTTON3", "ACTIONBUTTON4", "ACTIONBUTTON5", "ACTIONBUTTON6",
    "ACTIONBUTTON7", "ACTIONBUTTON8", "ACTIONBUTTON9", "ACTIONBUTTON10", "ACTIONBUTTON11", "ACTIONBUTTON12",
}

local function collectBindings()
    if not GetBindingKey then
        return nil
    end
    local result = {}
    for _, action in ipairs(BINDING_ACTIONS) do
        local first, second = GetBindingKey(action)
        if isExportable(first, "string") then
            table.insert(result, action .. ":" .. first)
        end
        if isExportable(second, "string") then
            table.insert(result, action .. ":" .. second)
        end
    end
    return #result > 0 and table.concat(result, ",") or nil
end

local function collectTrackedAchievements(includeCriteria)
    if not GetTrackedAchievements then
        return nil, nil
    end
    local tracked = { GetTrackedAchievements() }
    local achievementIds = {}
    local criteria = {}
    for _, achievementId in ipairs(tracked) do
        if isExportable(achievementId, "number") then
            achievementId = math.floor(achievementId)
            table.insert(achievementIds, tostring(achievementId))
            if includeCriteria and GetAchievementNumCriteria and GetAchievementCriteriaInfo then
                local count = GetAchievementNumCriteria(achievementId)
                if isExportable(count, "number") then
                    for index = 1, math.min(math.floor(count), 32) do
                        local _, _, completed, quantity, requiredQuantity, _, _, _, _, criteriaId =
                            GetAchievementCriteriaInfo(achievementId, index)
                        if isExportable(criteriaId, "number") and isExportable(completed, "boolean") and
                            isExportable(quantity, "number") and isExportable(requiredQuantity, "number") then
                            table.insert(criteria, string.format("%d:%d/%d:%d", math.floor(criteriaId),
                                math.floor(quantity), math.floor(requiredQuantity), completed and 1 or 0))
                            if #criteria >= 48 then
                                break
                            end
                        end
                    end
                end
            end
            if #achievementIds >= 16 or #criteria >= 48 then
                break
            end
        end
    end
    return #achievementIds > 0 and table.concat(achievementIds, ",") or nil,
        #criteria > 0 and table.concat(criteria, ",") or nil
end

function Context:SetEncounter(encounterId)
    self.encounterId = isExportable(encounterId, "number") and math.floor(encounterId) or nil
end

function Context:SetLastPlayerEvent(event)
    self.lastPlayerEvent = isExportable(event, "string") and event or nil
end

function Context:GetPublicSnapshot()
    local snapshot = {}
    local unavailable = {}
    local fields = ns.Settings:Get().bridgeFields or {}

    local function unavailableIfSelected(selected, name)
        if selected and snapshot[name] == nil then
            table.insert(unavailable, name)
        end
    end

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

    if fields.activity and IsInInstance then
        local _, instanceType = IsInInstance()
        if isExportable(instanceType, "string") then
            snapshot.activity = instanceType
        end
    end
    if fields.encounter and self.encounterId then
        snapshot.encounterId = self.encounterId
    end
    if fields.achievement or fields.criteria then
        local achievementIds, criteria = collectTrackedAchievements(fields.criteria)
        if fields.achievement then
            snapshot.achievementId = achievementIds
        end
        if fields.criteria then
            snapshot.criteria = criteria
        end
    end
    if fields.events and self.lastPlayerEvent then
        snapshot.event = self.lastPlayerEvent
    end
    if fields.skills then
        snapshot.skills = collectSkills()
    end
    if fields.talents then
        snapshot.talents = collectTalents()
    end
    if fields.actionSlots then
        snapshot.actionSlots = collectActions()
    end
    if fields.keyBindings then
        snapshot.keyBindings = collectBindings()
    end

    unavailableIfSelected(fields.class, "class")
    unavailableIfSelected(fields.specialization, "specialization")
    unavailableIfSelected(fields.level, "level")
    unavailableIfSelected(fields.zone, "zone")
    unavailableIfSelected(fields.map, "mapId")
    unavailableIfSelected(fields.activity, "activity")
    unavailableIfSelected(fields.encounter, "encounterId")
    unavailableIfSelected(fields.achievement, "achievementId")
    unavailableIfSelected(fields.criteria, "criteria")
    unavailableIfSelected(fields.skills, "skills")
    unavailableIfSelected(fields.talents, "talents")
    unavailableIfSelected(fields.actionSlots, "actionSlots")
    unavailableIfSelected(fields.keyBindings, "keyBindings")
    if #unavailable > 0 then
        snapshot.unavailable = table.concat(unavailable, ",")
    end

    return snapshot
end
