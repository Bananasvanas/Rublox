-- Rublox example game
print("Loading MyGame1...")

-- Map
if loadModel("Games/MyGame1/assets/map.glb") then
    local mapId = createEntity("Map")
    setEntityMesh(mapId, "Games/MyGame1/assets/map.glb")
    addCollider(mapId, 20, 1, 20, true)
else
    print("Map not found, creating empty world.")
end

-- Player
local playerId = createEntity("Player")
setEntityPosition(playerId, 0, 2, 0)
if loadModel("Games/MyGame1/assets/player.glb") then
    setEntityMesh(playerId, "Games/MyGame1/assets/player.glb")
end
addCollider(playerId, 1, 2, 1, false)

function onCollision(a, b)
    print("Collision: " .. a .. " <-> " .. b)
end

-- Controls
local W, A, S, D = 87, 65, 83, 68

function update(dt)
    -- Network events
    local event = getNetworkEvent()
    while event do
        if event.type == "data" then
            local cmd, id, x, y, z = event.data:match("(%w+) (%d+) (%-?[%d.]+) (%-?[%d.]+) (%-?[%d.]+)")
            if cmd == "POS" then
                setEntityPosition(tonumber(id), tonumber(x), tonumber(y), tonumber(z))
            end
        end
        event = getNetworkEvent()
    end

    -- Movement (only if not server, else server moves us)
    if not isServer then
        if isKeyDown(W) then moveEntity(playerId, 0, 0, -5*dt) end
        if isKeyDown(S) then moveEntity(playerId, 0, 0,  5*dt) end
        if isKeyDown(A) then moveEntity(playerId, -5*dt, 0, 0) end
        if isKeyDown(D) then moveEntity(playerId,  5*dt, 0, 0) end

        local x, y, z = getEntityPosition(playerId)
        sendData(string.format("POS %d %.2f %.2f %.2f", playerId, x, y, z))
    end
end